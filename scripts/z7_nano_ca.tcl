# Fixed client_ca partition only; the parent validates public PKI images.
set serial [lindex $argv 0]
set action [lindex $argv 1]
set backup_path [lindex $argv 2]
set image_path [lindex $argv 3]
source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]
proc must {name command} {
    set reply [transact $name "$command\r\n"]
    if {[string first "\r\nOK\r\n" $reply]<0} {error $name}
    return $reply
}
proc read_ca {offset size} {
    set reply [must ca_read$offset "AT+SYSFLASH=2,\"client_ca\",$offset,$size"]
    set prefix "+SYSFLASH:$size,"
    set start [string first $prefix $reply]
    if {$start<0} {error "Missing public CA data"}
    set first [expr {$start+[string length $prefix]}]
    return [string range $reply $first [expr {$first+$size-1}]]
}
proc write_ca {offset data} {
    set command "AT+SYSFLASH=1,\"client_ca\",$offset,[string length $data]\r\n"
    take_rx
    foreach char [split $command ""] {
        if {!([mrd -force -value 0x43c20004]&2)} {error "TX FIFO full"}
        scan $char %c code;mwr -force 0x43c20010 $code
    }
    set limit [expr {[clock milliseconds]+5000}];set prompt ""
    while {[string first ">" $prompt]<0} {
        append prompt [take_rx]
        if {[clock milliseconds]>$limit || [string length $prompt]>512 ||
            [string first "\r\nERROR\r\n" $prompt]>=0} {error "Flash write prompt missing"}
        after 10
    }
    # No CRLF: exactly the declared raw bytes, then wait for the write ACK.
    set reply [transact ca_write$offset $data]
    if {[string first "\r\nOK\r\n" $reply]<0} {error "CA write not acknowledged"}
}
try {
    must echo ATE0
    set version [must version AT+GMR]
    if {[string first "AT version:2.2.2.0-dev(s-952f658" $version]<0 ||
        [string first "AT+GMR" $version]>=0} {error "Unexpected firmware or echo"}
    set partitions [must partitions AT+SYSFLASH?]
    if {![regexp {\+SYSFLASH:"client_ca",64,7,0x[fF][cC]000,0x2000\r\n} $partitions]} {error "Unexpected CA partition"}
    set original ""
    for {set offset 0} {$offset<8192} {incr offset 512} {
        append original [read_ca $offset 512]
        puts "CA_STAGE read [expr {$offset+512}]";flush stdout
    }
    if {$action eq "backup"} {
        puts "CA_BACKUP [binary encode hex $original]";flush stdout
    } elseif {$action in {install restore}} {
        set f [open $backup_path rb];set expected [read $f];close $f
        if {$action eq "install" && $original ne $expected} {error "Live CA differs from verified backup"}
        set f [open $image_path rb];set image [read $f];close $f
        if {[string length $image]<12 || [string length $image]>8192 || [string length $image]%4} {error "Invalid CA image size"}
        must erase {AT+SYSFLASH=0,"client_ca",0,8192}
        puts "CA_STAGE erased 8192";flush stdout
        for {set offset 0} {$offset<[string length $image]} {incr offset 512} {
            set chunk [string range $image $offset [expr {$offset+511}]]
            write_ca $offset $chunk
            puts "CA_STAGE written [expr {$offset+[string length $chunk]}]";flush stdout
        }
        set verified ""
        for {set offset 0} {$offset<[string length $image]} {incr offset 512} {
            set size [expr {min(512,[string length $image]-$offset)}]
            append verified [read_ca $offset $size]
        }
        if {$verified ne $image} {error "CA readback mismatch"}
        puts "CA_STAGE verified [string length $image]";flush stdout
    } else {error "Unknown CA action"}
    puts "TLS_INVENTORY_COUNTERS [mrd -force -value 0x43c20020] [mrd -force -value 0x43c20024]"
    puts PASS_Z7_CA_OPERATION
} finally {
    # A failed binary write may still expect data: do not send speculative AT.
    disconnect
}
