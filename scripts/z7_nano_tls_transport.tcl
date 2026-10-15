# Key-free transport diagnostic only. Authentication is restored before exit.
set serial [lindex $argv 0]
source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]
proc must {name command} {
    set reply [transact $name "$command\r\n"]
    if {[string first "\r\nOK\r\n" $reply]<0} {error $name}
    return $reply
}
set changed 0
set attempted 0
set completed 0
set connected 0
set cleanup_ok 1
try {
    must echo ATE0
    set version [must version AT+GMR]
    if {[string first "AT version:" $version]<0 || [string first "AT+GMR" $version]>=0} {error echo}
    must store_off AT+SYSSTORE=0
    if {![regexp {\+SYSSTORE:0\r\n} [must store AT+SYSSTORE?]]} {error store}
    if {![regexp {\+CIPSSLCCONF:0,2,0,0\r\n} [must auth_before AT+CIPSSLCCONF?]]} {error auth}
    set changed 1
    must noauth_set AT+CIPSSLCCONF=0
    set attempted 1
    set reply [transact tls_noauth "AT+CIPSTART=\"SSL\",\"ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com\",443\r\n"]
    set connected [expr {[string first "\r\nOK\r\n" $reply]>=0}]
    set completed 1
} on error {message options} {
    puts TLS_TRANSPORT_STOP
} finally {
    if {$attempted} {
        set close_reply ""
        catch {set close_reply [transact cleanup_close "AT+CIPCLOSE\r\n"]}
        if {$connected && [string first "\r\nOK\r\n" $close_reply]<0} {set cleanup_ok 0}
    }
    if {$changed} {
        must restore_auth AT+CIPSSLCCONF=2,0,0
        if {![regexp {\+CIPSSLCCONF:0,2,0,0\r\n} [must auth AT+CIPSSLCCONF?]]} {error auth_restore}
    }
    puts "TLS_INVENTORY_COUNTERS [mrd -force -value 0x43c20020] [mrd -force -value 0x43c20024]"
    disconnect
}
if {$completed && $cleanup_ok} {puts PASS_Z7_TLS_TRANSPORT}
