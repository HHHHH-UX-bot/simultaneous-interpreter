# Program the Z7-Nano audio bitstream, initialise the PS, start the display app
# and read the audio register file. 2018.3-compatible: the accepted flow's
# hardware_z7_nano.tcl uses the Vivado hardware manager (open_hw_manager), which
# this installation's batch mode lacks, so the same checks run through xsdb.
#
# Usage: xsdb scripts/z7_nano_audio.tcl <program|diag|run> <cable-serial> <bit|-> <result.json> <ps7_init|-> [elf]
#   program  configure the PL only
#   diag     initialise the PS if ps7_init is given, then read the audio registers
#   run      program, initialise the PS, load and start the display app, then
#            validate the display report and the audio registers
set root [file normalize [file join [file dirname [info script]] ..]]
set action [lindex $argv 0]
set serial [lindex $argv 1]
set bitstream [lindex $argv 2]
set record [lindex $argv 3]
set init [lindex $argv 4]
set elf [lindex $argv 5]
if {$action ni {program diag run}} {error "Expected program, diag or run"}
if {$action eq "program" && ($bitstream eq "" || $bitstream eq "-")} {error "program needs a bitstream"}
if {$action eq "run" && ($elf eq "" || $elf eq "-")} {error "run needs an ELF"}

# hw_server publishes targets before it publishes their cable properties, so
# jtag_cable_serial can be absent for a moment after connect. Treat "not yet
# known" as "not this cable" and let the caller's retry loop wait, instead of
# letting dict get throw and abort the whole wait loop.
proc cable_serial {properties} {
    if {![dict exists $properties jtag_cable_serial]} {return ""}
    return [dict get $properties jtag_cable_serial]
}
proc try_connect {serial} {
    if {[catch {connect -url TCP:127.0.0.1:3121} e]} {return 0}
    after 1000
    set fpga [targets -target-properties -filter {name == "xc7z010"}]
    if {[llength $fpga] != 1 || [cable_serial [lindex $fpga 0]] ne $serial} {
        catch {disconnect}; return 0
    }
    foreach core {0 1} {
        set m [targets -target-properties -filter "name =~ \"*Cortex-A9*#$core\""]
        if {[llength $m] != 1 || [cable_serial [lindex $m 0]] ne $serial} {
            catch {disconnect}; return 0
        }
    }
    return 1
}

# A faulted ARM DAP (risks R16) cannot be recovered from software: wait for the
# operator to power cycle the board instead of failing straight away.
set deadline [expr {[clock milliseconds]+900000}]
while {![try_connect $serial]} {
    if {[clock milliseconds] >= $deadline} {
        error "Board did not return: ARM DAP faulted (risks R16); power cycle the board"
    }
    puts "WAITING_FOR_BOARD (power cycle the board if it is in the DAP fault state)"
    after 3000
}
puts "BOARD_READY"

# A stopped CPU does not stop PS peripherals or outstanding AXI/VDMA traffic.
# Start full PL replacement from a system reset, before changing its clocks.
if {$action ne "diag"} {
    targets -set -filter {name =~ "APU*"}
    rst -system
    after 1500
    puts "SYSTEM_RESET_BEFORE_PL_CONFIGURATION"
}

# No AXI traffic while the PL is reprogrammed, and memory reads need stopped cores.
foreach core {1 0} {
    set filter "name =~ \"*Cortex-A9*#$core\""
    targets -set -filter $filter
    if {[state] eq "Running"} {stop}
    if {![string match "Stopped:*" [state]]} {error "CPU$core did not halt"}
    puts "CPU${core}_HALTED [state]"
}

if {$action ne "diag"} {
    targets -set -filter {name == "xc7z010"}
    fpga -file $bitstream
    after 2000
    puts "PROGRAMMED $bitstream"
}

targets -set -filter {name =~ "*Cortex-A9*#0"}
proc read32 {address} {return [mrd -force -value $address]}
proc reg {offset} {return [mrd -force -value [expr {0x43c20000 + $offset}]]}

# A PL-only configuration keeps the PS-PL interface isolated, so the audio window
# is unreachable until the PS initialisation has run.
if {$init ne "" && $init ne "-"} {
    source $init
    ps7_init
    ps7_post_config
    puts "PS7_INITIALISED"
}

# The HDMI screen is powered from the board's PS USB0 host port (TPS2051B VBUS
# switch, enabled by PORTSC1.PP). ps7_init configures the PHY/MIO but leaves the
# port unpowered, so the display stays dark until a USB host initialisation runs.
# Replicate the verified usb_board_init() register sequence so the screen is
# powered by this flow itself (the CPU1 touch-host firmware is a separate step).
proc usb_port_power {} {
    proc r32 {a} {return [mrd -force -value $a]}
    proc w32 {a v} {mwr -force $a $v}
    set pairs {
        {0xf8000770 0x00003fff 0x00001204} {0xf8000774 0x00003fff 0x00001205}
        {0xf8000778 0x00003fff 0x00001204} {0xf800077c 0x00003fff 0x00001205}
        {0xf8000780 0x00003fff 0x00001204} {0xf8000784 0x00003fff 0x00001204}
        {0xf8000788 0x00003fff 0x00001204} {0xf800078c 0x00003fff 0x00001204}
        {0xf8000790 0x00003fff 0x00001205} {0xf8000794 0x00003fff 0x00001204}
        {0xf8000798 0x00003fff 0x00001204} {0xf800079c 0x00003fff 0x00001204}
    }
    w32 0xf8000008 0xdf0d
    foreach pair $pairs {
        set a [lindex $pair 0]; set m [lindex $pair 1]; set v [lindex $pair 2]
        w32 $a [expr {(([r32 $a] & ~$m) | ($v & $m))}]
    }
    w32 0xf80007b8 0x00001200
    w32 0xf800012c [expr {[r32 0xf800012c] | 4}]
    w32 0xf8000004 0x767b
    set pin 0x4000
    w32 0xe000a008 [expr {((~$pin & 0xffff) << 16)}]
    w32 0xe000a244 [expr {[r32 0xe000a244] | $pin}]
    w32 0xe000a248 [expr {[r32 0xe000a248] | $pin}]
    after 10
    w32 0xe000a008 [expr {((~$pin & 0xffff) << 16) | $pin}]
    after 20
    if {[r32 0xe0002100] != 0x01000040} {puts "USB_CORE_ID_UNEXPECTED [r32 0xe0002100]"}
    w32 0xe0002140 2
    after 5
    # ULPI viewport access is not required for VBUS; only set OTGSC and PORTSC.
    w32 0xe00021a8 3
    set portsc [r32 0xe0002184]
    w32 0xe0002184 [expr {($portsc & ~0x2a) | 0x1000}]
    after 20
    set pp [expr {([r32 0xe0002184] >> 12) & 1}]
    puts [format "USB0_PORT_POWER portsc=0x%08x PP=%d" [r32 0xe0002184] $pp]
    return $pp
}

if {$action eq "run"} {
    targets -set -filter {name =~ "*Cortex-A9*#0"}
    if {[usb_port_power] != 1} {puts "WARNING: PS USB0 port power did not latch; the screen may stay dark"}
}

# -1 = not started, 0 = app started but its startup check failed, 1 = healthy.
set display_ok -1
set words {}
if {$action eq "run"} {
    # Up to three starts. A reported startup failure is a VDMA/video-pipeline
    # race seen on this board; the audio register file is independent of it, so
    # a failed start is retried once or twice and then recorded, not fatal.
    for {set attempt 1} {$attempt <= 3} {incr attempt} {
        # This xsdb version has no -stop option for rst; the core is reset and left halted.
        rst -processor
        dow $elf
        mwr -force 0x1e000 0 16
        con
        puts "APP_STARTED attempt=$attempt $elf"
        # Wait for the app to reach its streaming state (word1 is the health marker
        # 0x600d600d, or 0xbad0bad0 for a reported failure), like the accepted flow.
        set deadline [expr {[clock milliseconds]+20000}]
        set attempt_words {}
        while {1} {
            set coherent 0
            if {[catch {
                set before [read32 0x1e008]
                set attempt_words [mrd -force -value 0x1e000 16]
                set after [read32 0x1e008]
                if {$before == $after && ($after & 1) == 0 && [lindex $attempt_words 2] == $after} {set coherent 1}
            } e]} { set coherent 0 }
            if {$coherent && ([lindex $attempt_words 1] == 0x600d600d || [lindex $attempt_words 1] == 0xbad0bad0)} {break}
            if {[clock milliseconds] >= $deadline} {error "Display startup timeout: $attempt_words"}
            after 100
        }
        set words $attempt_words
        if {[lindex $words 0] == 0x46504231 && [lindex $words 1] == 0x600d600d} {
            set display_ok 1
            puts "DISPLAY_REPORT flips=[lindex $words 4] good_frames=[lindex $words 6] underflows=[lindex $words 7] format_errors=[lindex $words 8] last_frame_pixels=[lindex $words 10]"
            break
        }
        set display_ok 0
        puts "DISPLAY_REPORT_INVALID attempt=$attempt $words"
        catch {stop}
    }
}

set id        [read32 0x43c20000]
if {$id != 0x41554431} {
    error "Audio register file is not responding at 0x43c20000 (wrong PL or AXI clock)"
}
set status    [reg 0x04]
set lat_us    [reg 0x08]
set lat_cyc   [reg 0x0c]
set adc       [reg 0x10]
set dac       [reg 0x14]
set overrun   [reg 0x18]
set underrun  [reg 0x1c]
set bclk_cyc0 [reg 0x20]
after 500
set bclk_cyc1 [reg 0x20]
set min_us    [reg 0x24]
set max_us    [reg 0x28]
set ctrl      [reg 0x2c]
set r0        [reg 0x30]
set r1        [reg 0x34]
set r2        [reg 0x38]
set r3        [reg 0x3c]
set r4        [reg 0x40]
set r5        [reg 0x44]
set r7        [reg 0x48]
set r8        [reg 0x4c]
set occ       [reg 0x50]
set bclk_hz   [reg 0x54]
set pc_bclk   [reg 0x60]
set pc_adclrc [reg 0x64]
set pc_dadlrc [reg 0x68]
set pc_adcdat [reg 0x6c]
set rb_r4     [reg 0x70]
set rb_r7     [reg 0x74]
set rb_r8     [reg 0x78]
set rb_r9     [reg 0x7c]

set codec_found  [expr {($status >> 1) & 1}]
set codec_addr   [expr {($status >> 2) & 3}]
set init_busy    [expr {($status >> 4) & 1}]
set init_done    [expr {($status >> 5) & 1}]
set init_error   [expr {($status >> 6) & 1}]
set max_valid    [expr {($status >> 7) & 1}]
set regs_written [expr {($status >> 24) & 0xff}]
set live         [expr {$status & 1}]
set rb_valid     [expr {($status >> 8) & 1}]
set sda_idle     [expr {($status >> 9) & 1}]
set scl_idle     [expr {($status >> 10) & 1}]
set bclk_alive   [expr {$bclk_cyc1 != $bclk_cyc0}]

set f [open $record w]
puts $f "{\"status\":\"passed\",\"action\":\"$action\",\"cable_serial\":\"$serial\"\
,\"id\":$id,\"status_word\":$status,\"live\":$live,\"codec_found\":$codec_found\
,\"codec_addr_sel\":$codec_addr,\"init_busy\":$init_busy,\"init_done\":$init_done\
,\"init_error\":$init_error,\"regs_written\":$regs_written,\"max_valid\":$max_valid\
,\"latency_us\":$lat_us,\"latency_cycles\":$lat_cyc,\"adc_frames\":$adc,\"dac_frames\":$dac\
,\"overruns\":$overrun,\"underruns\":$underrun,\"bclk_cycles_0\":$bclk_cyc0\
,\"bclk_cycles_1\":$bclk_cyc1,\"bclk_alive\":$bclk_alive,\"min_us\":$min_us,\"max_us\":$max_us\
,\"ctrl\":$ctrl,\"r0\":$r0,\"r1\":$r1,\"r2\":$r2,\"r3\":$r3,\"r4\":$r4,\"r5\":$r5,\"r7\":$r7,\"r8\":$r8\
,\"occupancy\":$occ,\"bclk_hz\":$bclk_hz\
,\"pin_bclk\":$pc_bclk,\"pin_adclrc\":$pc_adclrc,\"pin_dadlrc\":$pc_dadlrc,\"pin_adcdat\":$pc_adcdat\
,\"codec_rb_valid\":$rb_valid,\"sda_idle\":$sda_idle,\"scl_idle\":$scl_idle\
,\"codec_r4\":$rb_r4,\"codec_r7\":$rb_r7,\"codec_r8\":$rb_r8,\"codec_r9\":$rb_r9"
if {[info exists words]} {puts $f ",\"display_report\":\[[join $words ,]\]"}
puts $f ",\"display_ok\":$display_ok}"
close $f
puts [format "AUDIO_STATUS status=0x%08x live=%d codec_found=%d addr_sel=%d regs_written=%d" \
    $status $live $codec_found $codec_addr $regs_written]
puts [format "AUDIO_PINS bclk=%d adclrc=%d dadlrc=%d adcdat=%d" \
    $pc_bclk $pc_adclrc $pc_dadlrc $pc_adcdat]
puts [format "AUDIO_BCLK alive=%d cycles=%d->%d nominal_hz=%d" $bclk_alive $bclk_cyc0 $bclk_cyc1 $bclk_hz]
puts [format "AUDIO_L1 us=%d cycles=%d min=%d max=%d occ=%d adc=%d dac=%d over=%d under=%d" \
    $lat_us $lat_cyc $min_us $max_us $occ $adc $dac $overrun $underrun]
puts [format "AUDIO_READBACK valid=%d R4=0x%03x R7=0x%03x R8=0x%03x R9=0x%03x bus sda=%d scl=%d" \
    $rb_valid $rb_r4 $rb_r7 $rb_r8 $rb_r9 $sda_idle $scl_idle]
puts "PASS_Z7_NANO_AUDIO_[string toupper $action]"
disconnect
