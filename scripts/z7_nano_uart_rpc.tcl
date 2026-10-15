# Reuse the private Bridge pipe API on the current idle PS/UART platform.
set serial [lindex $argv 0]
source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]
fconfigure stdin -buffering line
fconfigure stdout -buffering line
puts "@@WIFI 0 OK ready"
while {[gets stdin line]>=0} {
    if {![regexp {^([0-9]+) (read|tx|drain|quit)(?: ([0-9a-fA-F]+))?$} $line -> id op argument]} {error "Invalid private RPC"}
    if {$op eq "quit"} {puts "@@WIFI $id OK closed";break}
    if {[catch {
        if {[mrd -force -value 0x1e184]!=0 || [mrd -force -value 0x1e148]!=0} {error "UART owner changed"}
        set result ""
        switch $op {
            read {
                scan $argument %x address
                if {$address<0 || $address>44 || $address%4} {error "UART register only"}
                set result [format %08x [mrd -force -value [expr {0x43c20000+$address}]]]
            }
            tx {
                if {[string length $argument]>512 || [string length $argument]%2} {error "TX bound"}
                foreach byte [regexp -all -inline {..} $argument] {
                    if {!([mrd -force -value 0x43c20004]&2)} {error "TX full"}
                    scan $byte %x value;mwr -force 0x43c20010 $value
                }
                set result [format %x [expr {[string length $argument]/2}]]
            }
            drain {
                scan $argument %x limit
                if {$limit<1 || $limit>2048} {error "RX bound"}
                set n [expr {min($limit,[mrd -force -value 0x43c20018])}]
                for {set i 0} {$i<$n} {incr i} {
                    set value [mrd -force -value 0x43c20014]
                    if {!($value&0x80000000)} {error "RX owner changed"}
                    append result [format %02x [expr {$value&255}]]
                }
            }
        }
    }]} {puts "@@WIFI $id ERROR operation_failed"} else {puts "@@WIFI $id OK $result"}
}
disconnect
