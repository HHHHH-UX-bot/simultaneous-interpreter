# Use the accepted programmer with the new platform's strict configuration ID.
set root [file normalize [file join [file dirname [info script]] ..]]
set f [open [file join $root scripts hardware_framebuffer.tcl] r]
set body [read $f]
close $f
if {[regexp -all {0x46564231} $body]!=1} {error "Programmer identity specialization is ambiguous"}
eval [string map {0x46564231 0x46574e31} $body]
