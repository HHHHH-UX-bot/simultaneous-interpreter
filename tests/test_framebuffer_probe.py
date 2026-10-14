"""Execute probe Tcl against isolated command stubs, never against Vivado/JTAG."""
from pathlib import Path
import tkinter
import unittest

SCRIPT=Path(__file__).resolve().parents[1]/'scripts/probe_framebuffer.tcl'

class ReadOnlyConfigurationProbe(unittest.TestCase):
    def probe(self,target='localhost:3121/xilinx_tcf/Digilent/210251A08870',
              done='0',eos='0',crc='0',identity='00000000',devices='xc7z010_1'):
        interpreter=tkinter.Tcl()
        interpreter.setvar('fixture_target',target)
        interpreter.setvar('fixture_devices',devices)
        interpreter.setvar('fixture_done',done)
        interpreter.setvar('fixture_eos',eos)
        interpreter.setvar('fixture_crc',crc)
        interpreter.setvar('fixture_identity',identity)
        interpreter.eval(r'''
            set calls {}
            set messages {}
            proc puts {args} {lappend ::messages [lindex $args end]}
            proc exit {code} {set ::fixture_exit $code}
            proc open_hw_manager {} {lappend ::calls open_hw_manager}
            proc connect_hw_server {args} {lappend ::calls connect_hw_server}
            proc get_hw_targets {} {return [list $::fixture_target]}
            proc current_hw_target {target} {lappend ::calls current_hw_target}
            proc open_hw_target {} {lappend ::calls open_hw_target}
            proc get_hw_devices {args} {
                if {$args ne {-filter {PART == "xc7z010"}}} {error "Device selection must be exact"}
                return $::fixture_devices
            }
            proc current_hw_device {device} {lappend ::calls current_hw_device}
            proc refresh_hw_device {args} {
                if {$args ne {-update_hw_probes false xc7z010_1}} {error "Unexpected probe refresh"}
                lappend ::calls refresh_hw_device
            }
            proc get_property {property device} {
                lappend ::calls $property
                switch -- $property {
                    REGISTER.IR.BIT5_DONE {return $::fixture_done}
                    REGISTER.CONFIG_STATUS.BIT04_END_OF_STARTUP_(EOS)_STATUS {return $::fixture_eos}
                    REGISTER.CONFIG_STATUS.BIT00_CRC_ERROR {return $::fixture_crc}
                    REGISTER.USR_ACCESS {return $::fixture_identity}
                    default {error "Unexpected property access: $property"}
                }
            }
            proc close_hw_target {} {lappend ::calls close_hw_target}
            proc disconnect_hw_server {} {lappend ::calls disconnect_hw_server}
            proc close_hw_manager {} {lappend ::calls close_hw_manager}
        ''')
        # No mrd/mwr/program commands exist in this interpreter. Introducing any
        # CPU/PL access or configuration into the probe makes the test fail.
        interpreter.eval(SCRIPT.read_text(encoding='utf-8'))
        return (int(interpreter.getvar('fixture_exit')),
                interpreter.splitlist(interpreter.getvar('calls')),
                interpreter.splitlist(interpreter.getvar('messages')))

    def test_unconfigured_status_is_read_only_and_closes_own_connections(self):
        status,calls,messages=self.probe()
        self.assertEqual(status,0)
        self.assertIn('FPGA_PROBE_RESULT 0 0 0 0',messages)
        self.assertEqual(calls[-3:],('close_hw_target','disconnect_hw_server','close_hw_manager'))

    def test_configured_status_decodes_usr_access(self):
        for identity in ('46564231','0x46564231','1180058161'):
            with self.subTest(identity=identity):
                status,calls,messages=self.probe(done='1',eos='1',identity=identity)
                self.assertEqual(status,0)
                self.assertIn('FPGA_PROBE_RESULT 1 1 0 1180058161',messages)

    def test_prefix_suffix_and_other_cables_are_rejected_before_target_open(self):
        for target in ('localhost:3121/xilinx_tcf/Digilent/210251A088701',
                       'localhost:3121/xilinx_tcf/Digilent/1210251A08870',
                       'localhost:3121/xilinx_tcf/Other/210251A08870'):
            with self.subTest(target=target):
                status,calls,messages=self.probe(target=target)
                self.assertEqual(status,1)
                self.assertNotIn('open_hw_target',calls)
                self.assertNotIn('PASS_FRAMEBUFFER_PROBE',messages)

    def test_missing_device_and_malformed_properties_fail(self):
        for fixture in ({'devices':''},{'done':'unknown'},{'identity':'garbage'},
                        {'identity':'-1'},{'identity':'4294967296'}):
            with self.subTest(fixture=fixture):
                status,calls,messages=self.probe(**fixture)
                self.assertEqual(status,1)
                self.assertNotIn('PASS_FRAMEBUFFER_PROBE',messages)
                self.assertEqual(calls[-3:],('close_hw_target','disconnect_hw_server','close_hw_manager'))

if __name__=='__main__': unittest.main()
