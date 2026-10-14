# Private process pipe only. Launch with -notrace -nolog -nojournal.
# Never print request lines or UART contents except hex in the pipe response.
proc reg_read {address} {
    global axis
    set txn [create_hw_axi_txn wifi_rpc_read $axis -type READ -address [format %08x $address] -len 1 -force]
    run_hw_axi $txn
    set data [string map {_ ""} [get_property DATA $txn]]
    if {![regexp {^[0-9a-fA-F]{1,8}$} $data]} {error "Unexpected AXI data format"}
    scan $data %x value
    return $value
}
proc reg_write {address value} {
    global axis
    set txn [create_hw_axi_txn wifi_rpc_write $axis -type WRITE -address [format %08x $address] -data [format %08x $value] -len 1 -force]
    run_hw_axi $txn
}
fconfigure stdin -buffering line
fconfigure stdout -buffering line
if {[catch {
    open_hw_manager
    connect_hw_server -url localhost:3121
    set targets [get_hw_targets]
    if {[llength $targets] != 1} {error "Target count"}
    current_hw_target [lindex $targets 0]
    open_hw_target
    set devices [get_hw_devices -filter {PART =~ "xc7z010*"}]
    if {[llength $devices] != 1} {error "Device count"}
    current_hw_device [lindex $devices 0]
    refresh_hw_device [lindex $devices 0]
    set axes [get_hw_axis]
    if {[llength $axes] != 1} {error "AXI count"}
    set axis [lindex $axes 0]
    reset_hw_axi $axis
    if {[reg_read 0] != 0x57494631} {error "Wrong mailbox identity"}
} init_error]} {
    puts "@@WIFI 0 ERROR init_failed"
    catch {close_hw_target}; catch {disconnect_hw_server}; catch {close_hw_manager}
    exit 1
}
puts "@@WIFI 0 OK ready"
while {[gets stdin line] >= 0} {
    if {![regexp {^([0-9]+) (read|write|tx|drain|quit)(?: ([0-9a-fA-F ]+))?$} $line -> id operation argument]} {
        puts "@@WIFI 0 ERROR invalid_request"
        continue
    }
    if {$operation eq "quit"} {puts "@@WIFI $id OK closed"; break}
    set failed [catch {
        set result ""
        switch $operation {
            read {
                if {![regexp {^[0-9a-fA-F]{1,8}$} $argument]} {error "Read argument"}
                scan $argument %x address
                set result [format %08x [reg_read $address]]
            }
            write {
                if {![regexp {^([0-9a-fA-F]{1,8}) ([0-9a-fA-F]{1,8})$} $argument -> address_hex value_hex]} {error "Write argument"}
                scan $address_hex %x address; scan $value_hex %x value
                reg_write $address $value
            }
            tx {
                if {![regexp {^[0-9a-fA-F]+$} $argument] || [string length $argument]>1024 || [string length $argument]%2} {error "TX argument"}
                set length [expr {[string length $argument]/2}]
                if {[reg_read 0x1c]+$length > 4096} {error "TX FIFO lacks space"}
                foreach byte [regexp -all -inline {..} $argument] {scan $byte %x value; reg_write 0x10 $value}
                set result [format %x $length]
            }
            drain {
                scan $argument %x limit
                if {$limit < 1 || $limit > 2048} {error "Drain bound"}
                set count [expr {min($limit,[reg_read 0x18])}]
                for {set i 0} {$i<$count} {incr i} {
                    set value [reg_read 0x14]
                    if {($value & 0x80000000) == 0} {error "RX FIFO unexpectedly empty"}
                    append result [format %02x [expr {$value & 255}]]
                }
            }
        }
    } rpc_error]
    if {$failed} {puts "@@WIFI $id ERROR operation_failed"} else {puts "@@WIFI $id OK $result"}
}
catch {delete_hw_axi_txn [get_hw_axi_txns wifi_rpc_read]}
catch {delete_hw_axi_txn [get_hw_axi_txns wifi_rpc_write]}
catch {close_hw_target}
catch {disconnect_hw_server}
catch {close_hw_manager}
