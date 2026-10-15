"""Execute the real Tcl RPC loop against an in-memory target; no JTAG or sockets."""
import tkinter
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
SCRIPT=(ROOT/'scripts/z7_nano_uart_rpc.tcl').read_text(encoding='utf-8')
MOCK=r'''
set argv {MOCK}; set replies {}; set writes {}; set regs [dict create]
proc source args {}
proc fconfigure args {}
proc puts args {lappend ::replies [lindex $args end]}
proc state {} {return Running}
proc disconnect {} {}
proc pilot_sample {} {return {827348051 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0}}
proc gets {channel name} {
    upvar 1 $name value
    if {![llength $::lines]} {return -1}
    set value [lindex $::lines 0]; set ::lines [lrange $::lines 1 end]
    return [string length $value]
}
proc mrd args {
    set at [expr {[lsearch -exact $args -value]+1}]
    if {$at==0} {error "Unexpected mock read"}
    set address [expr {[lindex $args $at]}]; set count [lindex $args [expr {$at+1}]]
    if {$count eq ""} {set count 1}
    set values {}
    for {set i 0} {$i<$count} {incr i} {
        set key [expr {$address+4*$i}]
        lappend values [expr {[dict exists $::regs $key]?[dict get $::regs $key]:0}]
    }
    return $values
}
proc mwr args {
    set address [expr {[lindex $args end-1]}];set value [lindex $args end]
    dict set ::regs $address $value;lappend ::writes [list $address $value]
}
'''

class CloudRpcTests(unittest.TestCase):
    def run_rpc(self,line,changes=()):
        t=tkinter.Tcl();t.eval(MOCK)
        # Actual public ABI identities; unspecified registers are idle/zero.
        for address,value in ((0x1e280,0x31505453),(0x1e2c0,0x31564c43),*changes):
            t.call('dict','set','::regs',address,value)
        t.setvar('lines',t.call('list',line,'99 quit'));t.eval(SCRIPT)
        return t,list(t.splitlist(t.getvar('replies')))
    def test_masks_write_only_fixed_pool_then_start(self):
        t,replies=self.run_rpc('1 cloud '+bytes(range(128)).hex())
        self.assertIn('@@WIFI 1 OK ',replies)
        writes=[tuple(map(int,t.splitlist(v))) for v in t.splitlist(t.getvar('writes'))]
        self.assertEqual(writes,[(0x1b0ff00+i,i) for i in range(128)]+[(0x1e2c8,1)])
    def test_short_mask_pool_rejected_without_write(self):
        t,replies=self.run_rpc('1 cloud 00')
        self.assertIn('@@WIFI 1 ERROR operation_failed',replies);self.assertFalse(t.getvar('writes'))
    def test_active_network_prevents_stop_or_mask_write(self):
        for request in ('1 cloud_stop','1 cloud '+('00'*128)):
            t,replies=self.run_rpc(request,((0x1e148,4),))
            self.assertIn('@@WIFI 1 ERROR operation_failed',replies);self.assertFalse(t.getvar('writes'))
    def test_stop_and_sample_use_cloud_abi(self):
        t,replies=self.run_rpc('1 cloud_stop')
        self.assertEqual(int(t.call('dict','get',t.getvar('regs'),0x1e2c8)),2)
        _,replies=self.run_rpc('1 cloud_sample')
        reply=[r for r in replies if r.startswith('@@WIFI 1 OK ')][0]
        self.assertEqual(int(reply.split()[3],16),0x31564c43)

if __name__=='__main__':unittest.main()
