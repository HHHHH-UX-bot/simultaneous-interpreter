# No API key or certificate/firmware writes. Wi-Fi join uses SYSSTORE=0.
set serial [lindex $argv 0]
set host [lindex $argv 1]
source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]
proc must {name command} {
    set reply [transact $name "$command\r\n"]
    if {[string first "\r\nOK\r\n" $reply]<0} {error $name}
    return $reply
}
set stage echo
set volatile_store 0
set name_changed 0
set tls_attempted 0
try {
    must echo ATE0
    set version [must version AT+GMR]
    if {[string first "AT version:" $version]<0 || [string first "AT+GMR" $version]>=0} {error echo}
    set stage volatile_store
    must store_off AT+SYSSTORE=0
    if {![regexp {\+SYSSTORE:0\r\n} [must store AT+SYSSTORE?]]} {error store}
    set volatile_store 1
    set stage station
    must station AT+CWMODE=1
    set stage join
    set private_join [gets stdin]
    must join [binary decode hex $private_join]
    unset private_join
    must address AT+CIFSR
    set stage dns
    must dns "AT+CIPDOMAIN=\"$host\""
    set stage time
    must sntp_set {AT+CIPSNTPCFG=1,0,"ntp.aliyun.com","ntp1.aliyun.com"}
    set synced 0
    for {set i 0} {$i<10} {incr i} {
        set reply [must time AT+CIPSNTPTIME?]
        if {[regexp {\+CIPSNTPTIME:([^\r\n]+)} $reply -> time_text] &&
            ![catch {clock scan $time_text -format {%a %b %e %H:%M:%S %Y} -gmt 1} epoch] &&
            abs($epoch-[clock seconds])<300} {set synced 1;break}
        after 1000
    }
    if {!$synced} {error time}
    set stage ssl_config
    must single AT+CIPMUX=0
    must auth_set AT+CIPSSLCCONF=2,0,0
    if {![regexp {\+CIPSSLCCONF:0,2,0,0\r\n} [must auth AT+CIPSSLCCONF?]]} {error auth}
    must sni_set "AT+CIPSSLCSNI=\"$host\""
    must common_name_set "AT+CIPSSLCCN=\"$host\""
    set name_changed 1
    set stage tls_positive
    set tls_attempted 1
    must tls_positive "AT+CIPSTART=\"SSL\",\"$host\",443"
    must close AT+CIPCLOSE
    set stage tls_wrong_identity
    must wrong_name {AT+CIPSSLCCN="wrong.invalid"}
    set reply [transact tls_wrong_identity "AT+CIPSTART=\"SSL\",\"$host\",443\r\n"]
    if {[string first "\r\nERROR\r\n" $reply]<0} {error wrong_identity_accepted}
    set stage tls_positive_again
    must restore_name "AT+CIPSSLCCN=\"$host\""
    must tls_positive_again "AT+CIPSTART=\"SSL\",\"$host\",443"
    must close_again AT+CIPCLOSE
    puts PASS_Z7_TLS_PREFLIGHT
} on error {message options} {
    # Never print arbitrary Tcl error details or a failed command argument.
    puts "TLS_PREFLIGHT_STOP $stage"
} finally {
    if {$tls_attempted} {catch {transact cleanup_close "AT+CIPCLOSE\r\n"}}
    if {$volatile_store && $name_changed} {catch {transact cleanup_name "AT+CIPSSLCCN=\"$host\"\r\n"}}
    puts "TLS_INVENTORY_COUNTERS [mrd -force -value 0x43c20020] [mrd -force -value 0x43c20024]"
    disconnect
}
