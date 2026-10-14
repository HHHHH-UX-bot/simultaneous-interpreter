# Common bounded target selection/halt helpers. No reset or memory writes here.
proc select_ps_core {number} {
    set pattern "*Cortex-A9*#$number"
    set deadline [expr {[clock milliseconds]+3000}]
    while {1} {
        set matches [targets -target-properties -filter "name =~ \"$pattern\""]
        if {[llength $matches]>1} {error "Expected exactly one Cortex-A9 core $number"}
        if {[llength $matches]==1} {
            set properties [lindex $matches 0]
            if {[dict exists $properties jtag_cable_serial] && [dict get $properties jtag_cable_serial] ne ""} {break}
        }
        # Hardware-server target discovery can publish a core before its
        # cable properties arrive. Never select it without identity proof.
        if {[clock milliseconds]>=$deadline} {error "No complete Cortex-A9 core $number identity"}
        after 50
    }
    if {[dict get $properties jtag_cable_serial] ne "210251A08870"} {
        error "Unexpected JTAG cable for core $number"
    }
    targets -set -filter "name =~ \"$pattern\""
    return $properties
}
proc halt_ps_core {number} {
    select_ps_core $number
    # Initial target-properties can contain a cached Running state. `state`
    # obtains current execution state and refreshes that cache.
    set live [state]
    if {$live eq "Running"} {
        if {[catch {stop} message options] && $message ne "Already stopped"} {
            return -options $options $message
        }
    } elseif {![string match "Stopped:*" $live]} {error "Unexpected CPU$number state: $live"}
    set live [state]
    if {![string match "Stopped:*" $live]} {error "CPU$number halt was not confirmed: $live"}
    puts "CPU${number}_HALTED $live"
}
