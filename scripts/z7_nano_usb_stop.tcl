# Caller must bind the expected board before invoking this USB0-only stop.
proc usb0_stop {} {
    if {([mrd -force -value 0xf800012c]&4)==0} {return}
    if {[mrd -force -value 0xe0002100]!=0x01000040} {error "USB0 capability mismatch before stop"}
    set mode [expr {[mrd -force -value 0xe00021a8]&3}]
    set command [mrd -force -value 0xe0002140]
    if {$mode!=3 && ($command&1)!=0} {error "Unknown running USB0 mode"}
    mwr -force 0xe0002148 0
    mwr -force 0xe0002140 [expr {$command&~1}]
    if {$mode==3} {
        set deadline [expr {[clock milliseconds]+1000}]
        while {([mrd -force -value 0xe0002144]&0x1000)==0} {
            if {[clock milliseconds]>=$deadline} {error "USB0 DMA halt not confirmed"}
            after 10
        }
    }
    if {[mrd -force -value 0xe0002140]&1} {error "USB0 still running"}
    puts USB0_DMA_HALTED
}
