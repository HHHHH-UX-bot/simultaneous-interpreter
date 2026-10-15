set serial [lindex $argv 0]
source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]
transact echo "ATE0\r\n"
set version [transact version "AT+GMR\r\n"]
if {[string first "AT version:" $version]<0 || [string first "SDK version:" $version]<0 ||
    [string first "AT+GMR" $version]>=0} {error "Fresh echo-off version proof required"}
foreach {name command} {
    store AT+SYSSTORE? partitions AT+SYSFLASH? heap AT+SYSRAM?
    auth AT+CIPSSLCCONF? sni AT+CIPSSLCSNI? common_name AT+CIPSSLCCN?
    sntp AT+CIPSNTPCFG? time AT+CIPSNTPTIME?
    ca_header {AT+SYSFLASH=2,"client_ca",0,12}
} {transact $name "$command\r\n"}
set overflow [mrd -force -value 0x43c20020]
set framing [mrd -force -value 0x43c20024]
puts "TLS_INVENTORY_COUNTERS $overflow $framing"
disconnect
puts PASS_Z7_TLS_INVENTORY
