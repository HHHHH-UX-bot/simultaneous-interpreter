"""Exact Tcl helper: incomplete discovery never authorizes selection or memory."""
import tkinter,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class Tests(unittest.TestCase):
    def runtime(self,mode):
        t=tkinter.Tcl();t.setvar('mode',mode)
        t.eval('''
          set discoveries 0;set selected 0;set stopped 0;set time_ms 0;set live Running
          rename clock real_clock
          proc clock {args} {incr ::time_ms 500;return $::time_ms}
          proc after {args} {}
          proc targets {args} {
            if {[lindex $args 0] eq "-set"} {incr ::selected;return}
            incr ::discoveries
            if {$::mode eq "zero"} {return {}}
            if {$::mode eq "multiple"} {return [list [dict create jtag_cable_serial 210251A08870] [dict create jtag_cable_serial 210251A08870]]}
            if {$::mode eq "missing" || ($::mode eq "delayed" && $::discoveries<3)} {return [list [dict create name {ARM Cortex-A9 MPCore #0}]]}
            if {$::mode eq "empty"} {return [list [dict create jtag_cable_serial {}]]}
            if {$::mode eq "wrong"} {return [list [dict create jtag_cable_serial OTHER]]}
            return [list [dict create jtag_cable_serial 210251A08870]]
          }
          proc state {} {return $::live}
          proc stop {} {incr ::stopped;set ::live {Stopped: User requested}}
          proc mrd {args} {error UNAUTHORIZED_MEMORY}
          proc mwr {args} {error UNAUTHORIZED_MEMORY}
        ''')
        t.eval((ROOT/'scripts/ps_debug_common.tcl').read_text(encoding='utf-8'));return t
    def test_delayed_identity_then_selection(self):
        t=self.runtime('delayed');t.eval('select_ps_core 0')
        self.assertEqual(int(t.getvar('discoveries')),3);self.assertEqual(int(t.getvar('selected')),1)
    def test_incomplete_or_ambiguous_never_selects(self):
        for mode in ('missing','empty','zero','multiple','wrong'):
            t=self.runtime(mode)
            with self.assertRaises(tkinter.TclError):t.eval('select_ps_core 0')
            self.assertEqual(int(t.getvar('selected')),0);self.assertEqual(int(t.getvar('stopped')),0)
            if mode in ('multiple','wrong'):self.assertEqual(int(t.getvar('discoveries')),1)
    def test_halt_requires_live_state_and_identity(self):
        t=self.runtime('delayed');t.eval('halt_ps_core 0');self.assertEqual(int(t.getvar('stopped')),1)
        t=self.runtime('wrong')
        with self.assertRaises(tkinter.TclError):t.eval('halt_ps_core 0')
        self.assertEqual(int(t.getvar('stopped')),0)
        t=self.runtime('ready');t.setvar('live','Unavailable')
        with self.assertRaises(tkinter.TclError):t.eval('halt_ps_core 0')
        self.assertEqual(int(t.getvar('stopped')),0)
if __name__=='__main__':unittest.main()

