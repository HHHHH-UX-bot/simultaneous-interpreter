# SDK 2018.3 Tcl can omit the Documents component when normalizing paths in
# a redirected Windows workspace. Keep an existing explicit absolute path if
# normalization produces a nonexistent path. File access still uses Tcl/Windows.
rename file z7_native_file
proc file {command args} {
    set result [uplevel 1 [linsert $args 0 z7_native_file $command]]
    if {$command eq "normalize" && [llength $args]==1} {
        set original [lindex $args 0]
        if {[regexp {^[A-Za-z]:[/\\]} $original] &&
            [z7_native_file exists $original] && ![z7_native_file exists $result]} {
            return [string map {\\ /} $original]
        }
    }
    return $result
}
set original_script [lindex $argv 0]
set argv [lrange $argv 1 end]
set argc [llength $argv]
source $original_script
