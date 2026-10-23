# Close the running packet-mode cloud application using public command/status
# mailboxes only. No credential window is read. Successful completion leaves
# CPU0 stopped for the subsequent owned CPU1 stop and full PL/PS reload.
# Usage: xsct close_cloud_for_tinyqwen.tcl 210241277120 result.json
if {$argc != 2} {error "Usage: close_cloud_for_tinyqwen.tcl cable_serial result.json"}
set cable [lindex $argv 0]
set result_file [lindex $argv 1]
if {![regexp {^[0-9]+$} $cable]} {error "Invalid cable serial"}
proc read32 {address} {return [lindex [mrd -force -value $address] 0]}
proc require_target {filter serial} {
    for {set i 0} {$i < 20} {incr i} {
        set found [targets -target-properties -filter $filter]
        if {[llength $found] == 1 && [dict exists [lindex $found 0] jtag_cable_serial]} {
            if {[dict get [lindex $found 0] jtag_cable_serial] ne $serial} {error "Wrong target cable"}
            return
        }
        after 500
    }
    error "Matching target identity unavailable"
}
proc public_ui_session {} {
    # Read only the published display snapshot's session enum. Validate its
    # fixed public DDR bounds and generation; never follow an arbitrary RAM pointer.
    for {set i 0} {$i < 40} {incr i} {
        set sequence [read32 0x1e084]
        if {$sequence&1} {after 50;continue}
        set base [read32 0x1e088];set stride [read32 0x1e08c];set active [read32 0x1e094]
        if {$base != 0x1b00000 || $stride < 64 || $stride > 0x40000 || $active > 1} {error "Public UI snapshot bounds mismatch"}
        set snapshot [expr {$base+$active*$stride}]
        if {[read32 $snapshot] != 0x55495331} {error "Public UI snapshot ABI mismatch"}
        set session [read32 [expr {$snapshot+36}]]
        if {[read32 [expr {$snapshot+24}]] == [read32 0x1e09c] && $sequence == [read32 0x1e084]} {return $session}
        after 50
    }
    error "Stable public UI session unavailable"
}
proc pause_ui {} {
    if {[read32 0x1e080] != 0x55493131 || [read32 0x1e0c0] != 0x55494331} {error "UI public ABI mismatch"}
    set session [public_ui_session]
    if {$session == 2} {puts UI_ALREADY_PAUSED;flush stdout;return paused}
    if {$session == 0 && [read32 0x1e148] == 0} {puts UI_ALREADY_IDLE;flush stdout;return idle}
    set request [expr {([read32 0x1e0c4]+1)&0xffffffff}]
    mwr -force 0x1e0c8 [read32 0x1e0a8]
    mwr -force 0x1e0cc 2
    mwr -force 0x1e0c4 $request
    for {set i 0} {$i < 100} {incr i} {
        if {[read32 0x1e0d0] == $request} {
            if {[read32 0x1e0d4] != 1} {error "UI_PAUSE rejected"}
            puts "UI_PAUSE_ACK";flush stdout;return paused
        }
        after 50
    }
    error "UI_PAUSE acknowledgement timeout"
}
proc wait_paused_transport {} {
    set deadline [expr {[clock milliseconds]+30000}]
    set previous -1
    while {[clock milliseconds] < $deadline} {
        set phase [read32 0x1e148];set error_code [read32 0x1e14c]
        if {$phase != $previous} {puts "PAUSE_NETWORK phase=$phase error=$error_code";flush stdout;set previous $phase}
        if {$error_code != 0} {error "Network error while waiting for pause: $error_code"}
        if {$phase == 8 || $phase == 0} {return $phase}
        after 100
    }
    error "Paused TLS phase 8 / already closed phase 0 not confirmed"
}
proc close_owned_transport {} {
    set request [expr {([read32 0x1e150]+1)&0xffffffff}]
    # Publish an empty action-4 request, then its sequence. The credential
    # payload is never read or logged; it is cleared as part of this request.
    mwr -force 0x1e180 0x3146434e
    mwr -force 0x1e188 4
    mwr -force 0x1e18c 0
    mwr -force 0x1e190 0
    mwr -force 0x1e194 0 25
    mwr -force 0x1e1f8 0
    mwr -force 0x1e1fc 0
    mwr -force 0x1e184 $request
    set deadline [expr {[clock milliseconds]+30000}]
    while {[clock milliseconds] < $deadline} {
        set phase [read32 0x1e148];set error_code [read32 0x1e14c]
        if {$error_code != 0} {error "Owned close failed: network error $error_code"}
        if {$phase == 0 && [read32 0x1e150] == $request} {puts "NCF_ACTION4_IDLE";flush stdout;return}
        after 100
    }
    error "NCF action 4 closure not acknowledged at phase 0"
}
proc uart_send {text} {
    foreach char [split $text ""] {
        if {![expr {[read32 0x43c30004]&2}]} {error "UART transmit FIFO unavailable"}
        scan $char %c value
        mwr -force 0x43c30010 $value
    }
}
proc uart_reply {} {
    set reply "";set deadline [expr {[clock milliseconds]+3000}]
    while {[clock milliseconds] < $deadline} {
        while {[read32 0x43c30004]&1} {
            set value [read32 0x43c30014]
            if {!($value&0x80000000)} {error "UART ownership changed"}
            if {[string length $reply] >= 2048} {error "Unexpected oversized AT response"}
            append reply [format %c [expr {$value&255}]]
        }
        if {[string first "\r\nOK\r\n" $reply]>=0 || [string first "\r\nERROR\r\n" $reply]>=0} {return $reply}
        after 5
    }
    error "AT idle probe timeout"
}
set halted 0
if {[catch {
    connect -url TCP:127.0.0.1:3121
    require_target {name == "xc7z010"} $cable
    require_target {name =~ "*Cortex-A9*#0"} $cable
    require_target {name =~ "*Cortex-A9*#1"} $cable
    targets -set -filter {name =~ "*Cortex-A9*#1"}
    # A new 2018.3 TCF connection can briefly report the cached Running state
    # before its existing suspended-core event arrives. Wait for confirmation.
    for {set i 0} {$i<20 && ![string match "Stopped:*" [state]]} {incr i} {after 100}
    if {![string match "Stopped:*" [state]]} {error "Stop the matching owned CPU1 touch host before closing CPU0"}
    targets -set -filter {name =~ "*Cortex-A9*#0"}
    if {[state] ne "Running" || [read32 0x1e140] != 0x3154454e} {error "Running known cloud application required"}
    set ui_quiesced_state [pause_ui]
    set paused_phase [wait_paused_transport]
    if {$paused_phase == 8} {close_owned_transport}
    # Newer firmware can complete the same guarded close during UI_PAUSE.
    if {[read32 0x1e148] != 0 || [read32 0x1e14c] != 0} {error "Network idle/error0 required before UART ownership"}
    if {[read32 0x43c30000] != 0x57494631 || [read32 0x43c30008] != 25} {error "Known 50 MHz packet UART divisor 25 required; baud will not be changed"}
    stop
    set halted 1
    if {![string match "Stopped:*" [state]]} {error "CPU0 halt not confirmed"}
    uart_send "AT+CIPMODE?\r\n"
    set mode [uart_reply]
    if {[string first "+CIPMODE:0" $mode]<0 || [string first "\r\nOK\r\n" $mode]<0} {error "Ordinary packet AT mode not confirmed"}
    uart_send "AT+CIPSTATUS\r\n"
    set status [uart_reply]
    if {[string first "\r\nOK\r\n" $status]<0 || [string first "STATUS:3" $status]>=0 || [string first "+CIPSTATUS:" $status]>=0} {error "No-active-TCP state not confirmed"}
    uart_send "AT\r\n"
    if {[string first "\r\nOK\r\n" [uart_reply]]<0} {error "Final AT acknowledgement missing"}
    set handle [open $result_file w]
    puts $handle "{\"status\":\"passed\",\"cable_serial\":\"$cable\",\"ui_quiesced\":true,\"ui_state\":\"$ui_quiesced_state\",\"network_phase\":0,\"network_error\":0,\"packet_at_mode\":0,\"active_tcp\":false,\"uart_baud\":2000000,\"uart_divisor\":25,\"cpu0_stopped\":true}"
    close $handle
    disconnect
    puts PASS_CLOUD_CLOSED_FOR_TINYQWEN
} failure options]} {
    if {$halted} {catch {con}}
    catch {disconnect}
    return -options $options $failure
}
