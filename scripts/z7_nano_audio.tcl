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

proc try_connect {serial} {
    if {[catch {connect -url TCP:127.0.0.1:3121} e]} {return 0}
    after 1000
    set fpga [targets -target-properties -filter {name == "xc7z010"}]
    if {[llength $fpga] != 1 || [dict get [lindex $fpga 0] jtag_cable_serial] ne $serial} {
        catch {disconnect}; return 0
    }
    foreach core {0 1} {
        set m [targets -target-properties -filter "name =~ \"*Cortex-A9*#$core\""]
        if {[llength $m] != 1 || [dict get [lindex $m 0] jtag_cable_serial] ne $serial} {
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

if {$action eq "run"} {
    # This xsdb version has no -stop option for rst; the core is reset and left halted.
    rst -processor
    dow $elf
    mwr -force 0x1e000 0 16
    con
    puts "APP_STARTED $elf"
    # Wait for the app to reach its streaming state (word1 is the health marker
    # 0x600d600d, or 0xbad0bad0 for a reported failure), like the accepted flow.
    set deadline [expr {[clock milliseconds]+20000}]
    set words {}
    while {1} {
        set coherent 0
        if {[catch {
            set before [read32 0x1e008]
            set words [mrd -force -value 0x1e000 16]
            set after [read32 0x1e008]
            if {$before == $after && ($after & 1) == 0 && [lindex $words 2] == $after} {set coherent 1}
        } e]} { set coherent 0 }
        if {$coherent && ([lindex $words 1] == 0x600d600d || [lindex $words 1] == 0xbad0bad0)} {break}
        if {[clock milliseconds] >= $deadline} {error "Display startup timeout: $words"}
        after 100
    }
    if {[lindex $words 0] != 0x46504231 || [lindex $words 1] != 0x600d600d} {
        error "Display report invalid: $words"
    }
    puts "DISPLAY_REPORT flips=[lindex $words 4] good_frames=[lindex $words 6] underflows=[lindex $words 7] format_errors=[lindex $words 8] last_frame_pixels=[lindex $words 10]"
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

set codec_found  [expr {($status >> 1) & 1}]
set codec_addr   [expr {($status >> 2) & 3}]
set init_busy    [expr {($status >> 4) & 1}]
set init_done    [expr {($status >> 5) & 1}]
set init_error   [expr {($status >> 6) & 1}]
set max_valid    [expr {($status >> 7) & 1}]
set regs_written [expr {($status >> 24) & 0xff}]
set live         [expr {$status & 1}]
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
,\"pin_bclk\":$pc_bclk,\"pin_adclrc\":$pc_adclrc,\"pin_dadlrc\":$pc_dadlrc,\"pin_adcdat\":$pc_adcdat}"
if {[info exists words]} {puts $f ",\"display_report\":\[[join $words ,]\]"}
close $f
puts [format "AUDIO_STATUS status=0x%08x live=%d codec_found=%d addr_sel=%d regs_written=%d" \
    $status $live $codec_found $codec_addr $regs_written]
puts [format "AUDIO_PINS bclk=%d adclrc=%d dadlrc=%d adcdat=%d" \
    $pc_bclk $pc_adclrc $pc_dadlrc $pc_adcdat]
puts [format "AUDIO_BCLK alive=%d cycles=%d->%d nominal_hz=%d" $bclk_alive $bclk_cyc0 $bclk_cyc1 $bclk_hz]
puts [format "AUDIO_L1 us=%d cycles=%d min=%d max=%d occ=%d adc=%d dac=%d over=%d under=%d" \
    $lat_us $lat_cyc $min_us $max_us $occ $adc $dac $overrun $underrun]
puts "PASS_Z7_NANO_AUDIO_[string toupper $action]"
disconnect
