# Reuse the private Bridge pipe API on the current idle PS/UART platform.
set serial [lindex $argv 0]
source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]
fconfigure stdin -buffering line
fconfigure stdout -buffering line
puts "@@WIFI 0 OK ready"
set tcp_read 0
proc cloud_sample {} {
    set deadline [expr {[clock milliseconds]+3000}]
    while {[clock milliseconds]<$deadline} {
        set before [mrd -force -value 0x1e2c4]
        set words [mrd -force -value 0x1e2c0 16]
        set after [mrd -force -value 0x1e2c4]
        if {$before==$after && ($after&1)==0 && [lindex $words 1]==$after &&
            [lindex $words 0]==0x31564c43} {return $words}
    }
    error "No coherent cloud report"
}
while {[gets stdin line]>=0} {
    if {![regexp {^([0-9]+) (read|tx|drain|load|pilot|sample|tcp|cloud|cloud_sample|cloud_text|cloud_stop|quit)(?: ([0-9a-fA-F]+))?$} $line -> id op argument]} {error "Invalid private RPC"}
    if {$op eq "quit"} {puts "@@WIFI $id OK closed";break}
    if {[catch {
        if {[state] ne "Running" || [mrd -force -value 0x1e184]!=0 ||
            [mrd -force -value 0x1e280]!=0x31505453} {error "Application owner changed"}
        if {$op in {read tx drain load cloud cloud_stop}} {
            set owner [pilot_sample]
            if {[mrd -force -value 0x1e148]!=0 || [lindex $owner 2] ni {0 3} ||
                [lindex $owner 4]!=[lindex $owner 5]} {error "UART owner changed"}
        }
        set result ""
        switch $op {
            cloud - cloud_stop {
                cloud_sample
                if {$op eq "cloud"} {
                    if {[string length $argument]!=256} {error "Control mask pool bound"}
                    set offset 0
                    foreach byte [regexp -all -inline {..} $argument] {
                        scan $byte %x value;mwr -force -size b [expr {0x1b0ff00+$offset}] $value;incr offset
                    }
                    mwr -force 0x1e2c8 1
                } else {mwr -force 0x1e2c8 2}
            }
            cloud_sample {
                set words [cloud_sample]
                foreach value $words {lappend formatted [format %x $value]}
                set result [join $formatted " "];unset formatted
            }
            cloud_text {
                set before [mrd -force -value 0x1b0e000]
                set result [binary encode hex [mrd -force -size b -bin 0x1b0e000 2052]]
                set after [mrd -force -value 0x1b0e000]
                if {$before!=$after || ($after&1)} {error "Cloud caption changed during read"}
            }
            sample {
                set words [pilot_sample]
                foreach value $words {lappend formatted [format %x $value]}
                set result [join $formatted " "];unset formatted
            }
            load {
                if {[mrd -force -value 0x1e288]!=0 || [mrd -force -value 0x1e290] ni {0 3}} {error "Pilot is active"}
                scan $argument %x size
                set input [file join [pwd] stream_file.bin]
                if {$size<12 || $size>0x80000 || [file size $input]!=$size} {error "File bound"}
                set f [open $input rb];set expected [read $f];close $f
                mwr -force -bypass-cache-sync -size b -bin -file $input 0x1c00000 $size
                set actual [mrd -force -size b -bin 0x1c00000 $size]
                if {$actual ne $expected} {error "DDR file readback differs"}
                set tcp_read 0;set result [format %x $size]
            }
            pilot {
                scan $argument %x request
                if {$request ni {1 2 3}} {error "Pilot request"}
                set phase [mrd -force -value 0x1e288]
                if {$request==1 && ($phase!=0 || [mrd -force -value 0x1e148]!=0)} {error "Pilot is not fresh"}
                if {$request==2 && $phase ni {1 2 6}} {error "No stream owner"}
                if {$request==3 && ($phase!=3 || [mrd -force -value 0x1e148]!=0)} {error "Owner must be released before reset"}
                mwr -force 0x1e290 $request;set result $argument
            }
            tcp {
                if {[mrd -force -value 0x1e2c0]==0x31564c43 && [mrd -force -value 0x1e2d0]!=0} {error "Cloud consumer owns RX"}
                scan $argument %x end
                if {$end<$tcp_read || $end>0x80000 || $end>[mrd -force -value 0x1e2a0]} {error "RX capture bound"}
                set size [expr {min(8192,$end-$tcp_read)}]
                if {$size>0} {
                    set result [binary encode hex [mrd -force -size b -bin [expr {0x1c80000+$tcp_read}] $size]]
                    incr tcp_read $size
                }
            }
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
