"""Host stage guard checks. Dummy outputs; no board/tool execution."""
import json
from pathlib import Path
import sys,unittest,uuid
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
import run_framebuffer as driver

class FramebufferGuards(unittest.TestCase):
    def setUp(self):
        self.out=ROOT/'tmp/framebuffer_host_tests'/uuid.uuid4().hex[:10]
        self.out.mkdir(parents=True)
        self.redirect=patch.object(driver,'OUT',self.out); self.redirect.start()
        self.ps_redirect=patch.object(driver,'PS_FAILURE_STATE',self.out/'ps_failure_state.json'); self.ps_redirect.start()
        self.files={'framebuffer_top.bit':b'nonexecutable bit fixture',
                    'framebuffer_app.elf':b'nonexecutable ELF fixture','ps7_init.tcl':b'# fixture only'}
        for name,contents in self.files.items(): (self.out/name).write_bytes(contents)
        self.build={'Status':'passed','BitstreamSHA256':driver.sha(self.out/'framebuffer_top.bit'),
                    'XSASHA256':'fixture','PSInitSHA256':driver.sha(self.out/'ps7_init.tcl')}
        self.app={'Status':'passed','ELFSHA256':driver.sha(self.out/'framebuffer_app.elf')}
        driver.save('program.json',{'Status':'passed','Attempt':'known_program','BitstreamSHA256':self.build['BitstreamSHA256']})
        driver.save('failure_state.json',{'CPUState':'running_animation','OwnTreeStopped':True})
        driver.save('active_app.json',{'Status':'ready','RunAttempt':'previous_run','ProgramAttempt':'known_program'})
        from argparse import Namespace
        self.args=Namespace(action='run',xsdb=Path('not_a_real_tool'),vivado=Path('not_a_real_vivado'),gcc=None)

    def tearDown(self): self.ps_redirect.stop(); self.redirect.stop()

    def guarded_execute(self):
        with (patch.object(driver,'checked_build',return_value=self.build),
              patch.object(driver,'checked_record',return_value=self.app),
              patch.object(driver.runner,'command') as command,
              patch.object(driver.runner,'cleanup') as cleanup):
            result=driver.execute(self.args)
            return result,command,cleanup

    def test_consumed_program_refuses_reinit_without_halting_current_animation(self):
        driver.save('program_consumption.json',{'ProgramAttempt':'known_program'})
        result,command,cleanup=self.guarded_execute()
        self.assertEqual(result,1); command.assert_not_called(); cleanup.assert_not_called()
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text())['CPUState'],'running_animation')
        self.assertEqual(json.loads((self.out/'active_app.json').read_text())['RunAttempt'],'previous_run')

    def test_replaced_canonical_and_matching_snapshot_still_fail_bound_hash(self):
        original=driver.shutil.copyfile
        def replace_then_copy(source,target):
            if Path(source).name=='framebuffer_top.bit': Path(source).write_bytes(b'replaced after precheck')
            return original(source,target)
        with patch.object(driver.shutil,'copyfile',side_effect=replace_then_copy):
            result,command,cleanup=self.guarded_execute()
        self.assertEqual(result,1); command.assert_not_called(); cleanup.assert_not_called()
        self.assertIn('Snapshot mismatch',json.loads((self.out/'run.json').read_text())['Error'])

    def test_failed_program_validation_invalidates_latest_attempt(self):
        self.args.action='program'
        self.args.vivado=Path('not_a_real_tool')
        with patch.object(driver,'checked_build',side_effect=RuntimeError('stale build')),patch.object(driver.runner,'command') as command:
            self.assertEqual(driver.execute(self.args),1)
        command.assert_not_called()
        self.assertEqual(json.loads((self.out/'program.json').read_text())['Status'],'failed')

    def test_reprogram_quiesces_dma_before_programming(self):
        self.args.action='program'; self.args.vivado=Path('not_a_real_tool')
        driver.save('current_pl.json',{'Status':'framebuffer','ProgramAttempt':'known_program'})
        calls=[]
        def fake_command(args,*other,**kwargs):
            calls.append([str(a) for a in args])
            if any('probe_framebuffer.tcl' in a for a in calls[-1]): return 'FPGA_PROBE_RESULT 1 1 0 1180058161\nPASS_FRAMEBUFFER_PROBE'
            if any('ps_quiesce_framebuffer.tcl' in a for a in calls[-1]): return 'PASS_FRAMEBUFFER_QUIESCE'
            return 'FPGA_RESULT 1 1 0 1180058161\nPASS_FRAMEBUFFER_PROGRAM'
        with (patch.object(driver,'checked_build',return_value=self.build),
              patch.object(driver,'checked_record',return_value=self.app),
              patch.object(driver.runner,'command',side_effect=fake_command)):
            self.assertEqual(driver.execute(self.args),0)
        self.assertEqual(len(calls),3)
        self.assertTrue(any('probe_framebuffer.tcl' in a for a in calls[0]))
        self.assertTrue(any('ps_quiesce_framebuffer.tcl' in a for a in calls[1]))
        self.assertTrue(any('hardware_framebuffer.tcl' in a for a in calls[2]))

    def live_execute(self,live='1 1 0 1180058161',probe_error=None,stop_state=None):
        calls=[]
        def command(args,*other,**kwargs):
            calls.append([str(a) for a in args])
            if any('probe_framebuffer.tcl' in a for a in calls[-1]):
                if probe_error is not None: raise probe_error
                return 'FPGA_PROBE_RESULT '+live+'\nPASS_FRAMEBUFFER_PROBE'
            if any('ps_quiesce_framebuffer.tcl' in a for a in calls[-1]): return 'PASS_FRAMEBUFFER_QUIESCE'
            if any('hardware_framebuffer.tcl' in a for a in calls[-1]): return 'FPGA_RESULT 1 1 0 1180058161\nPASS_FRAMEBUFFER_PROGRAM'
            raise AssertionError('Unexpected CPU/PL command after the probe: '+str(calls[-1]))
        if stop_state is None: stop_state={'CPUState':'confirmed_stopped','OwnTreeStopped':True}
        with (patch.object(driver,'checked_build',return_value=self.build),
              patch.object(driver,'checked_record',return_value=self.app),
              patch.object(driver.runner,'command',side_effect=command),
              patch.object(driver.runner,'cleanup',return_value=stop_state) as cleanup):
            result=driver.execute(self.args)
        return result,calls,cleanup

    def test_done_zero_ignores_historical_framebuffer_and_halts_without_pl_mmio(self):
        self.args.action='program'
        driver.save('current_pl.json',{'Status':'framebuffer','ProgramAttempt':'historical'})
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,0); cleanup.assert_called_once()
        self.assertEqual(len(calls),2)
        self.assertIn('probe_framebuffer.tcl',calls[0][-1])
        self.assertTrue(any('hardware_framebuffer.tcl' in arg for arg in calls[1]))
        self.assertFalse(any('ps_quiesce_framebuffer.tcl' in arg for call in calls for arg in call))
        self.assertTrue(json.loads((self.out/'program.json').read_text())['UnconfiguredPLBaseline'])

    def test_live_framebuffer_quiesces_even_without_current_pl_record(self):
        self.args.action='program'
        result,calls,cleanup=self.live_execute()
        self.assertEqual(result,0); cleanup.assert_not_called()
        self.assertTrue(any('ps_quiesce_framebuffer.tcl' in arg for arg in calls[1]))

    def test_unknown_configured_identity_rejects_without_cpu_or_pl_mutation(self):
        self.args.action='program'
        driver.save('current_pl.json',{'Status':'framebuffer'})
        result,calls,cleanup=self.live_execute('1 1 0 12345')
        self.assertEqual(result,1); self.assertEqual(len(calls),1); cleanup.assert_not_called()
        self.assertEqual(json.loads((self.out/'active_app.json').read_text())['RunAttempt'],'previous_run')
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text())['CPUState'],'running_animation')

    def test_cpu_unknown_blocks_even_read_only_probe(self):
        self.args.action='program'
        driver.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':True})
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,1); self.assertEqual(calls,[]); cleanup.assert_not_called()

    def test_unconfirmed_ps_process_tree_blocks_probe(self):
        self.args.action='probe'
        driver.save('ps_failure_state.json',{'CPUState':'confirmed_stopped','OwnTreeStopped':False})
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,1); self.assertEqual(calls,[]); cleanup.assert_not_called()

    def test_unconfirmed_previous_probe_tree_blocks_before_new_probe(self):
        self.args.action='probe'
        driver.save('probe_operation.json',{'Status':'pending','OwnTreeStopped':False,'OriginalProcessID':999})
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,1); self.assertEqual(calls,[]); cleanup.assert_not_called()

    def test_probe_failure_is_not_hidden_by_previous_success(self):
        self.args.action='program'
        driver.save('current_pl.json',{'Status':'framebuffer'})
        result,calls,cleanup=self.live_execute(probe_error=driver.runner.StageError('probe failed',True))
        self.assertEqual(result,1); self.assertEqual(len(calls),1); cleanup.assert_not_called()
        self.assertEqual(json.loads((self.out/'program.json').read_text())['Status'],'failed')
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text())['CPUState'],'running_animation')

    def test_unterminated_probe_process_creates_unknown_gate_without_cpu_cleanup(self):
        self.args.action='program'
        result,calls,cleanup=self.live_execute(probe_error=driver.runner.StageError('probe tree unknown',False,999))
        self.assertEqual(result,1); self.assertEqual(len(calls),1); cleanup.assert_not_called()
        state=json.loads((self.out/'failure_state.json').read_text())
        self.assertEqual(state['CPUState'],'unknown'); self.assertFalse(state['OwnTreeStopped'])
        self.assertFalse(json.loads((self.out/'probe_operation.json').read_text())['OwnTreeStopped'])

    def test_run_after_power_cycle_refuses_pl_mmio_and_does_not_consume_program(self):
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,1); self.assertEqual(len(calls),1); cleanup.assert_not_called()
        self.assertFalse((self.out/'program_consumption.json').exists())
        self.assertEqual(json.loads((self.out/'active_app.json').read_text())['RunAttempt'],'previous_run')

    def test_sample_after_power_cycle_refuses_pl_mmio(self):
        self.args.action='sample'
        driver.save('attempts/previous_run/result.json',{'Status':'passed',
            'BitstreamSHA256':self.build['BitstreamSHA256'],'ELFSHA256':self.app['ELFSHA256']})
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,1); self.assertEqual(len(calls),1); cleanup.assert_not_called()

    def test_unconfigured_probe_does_not_establish_cpu_stopped(self):
        self.args.action='probe'
        result,calls,cleanup=self.live_execute('0 0 0 0')
        self.assertEqual(result,0); self.assertEqual(len(calls),1); cleanup.assert_not_called()
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text())['CPUState'],'running_animation')

    def test_done_zero_needs_actual_successful_cpu_halt_before_configuration(self):
        self.args.action='program'
        result,calls,cleanup=self.live_execute('0 0 0 0',stop_state={'CPUState':'unknown','OwnTreeStopped':True})
        self.assertEqual(result,1); self.assertEqual(len(calls),1)
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text())['CPUState'],'unknown')

    def test_faulted_configured_status_refuses_mutation(self):
        self.args.action='program'
        for live in ('1 0 0 1180058161','1 1 1 1180058161'):
            with self.subTest(live=live):
                result,calls,cleanup=self.live_execute(live)
                self.assertEqual(result,1); self.assertEqual(len(calls),1); cleanup.assert_not_called()

    def test_fpga_status_rejects_malformed_and_duplicate_reports(self):
        for text in ('FPGA_PROBE_RESULT 1 1 0', 'FPGA_PROBE_RESULT 2 1 0 1180058161',
                     'FPGA_PROBE_RESULT 1 1 0 -1', 'FPGA_PROBE_RESULT 1 1 0 4294967296',
                     'FPGA_PROBE_RESULT 1 1 0 1180058161\nFPGA_PROBE_RESULT 0 0 0 0'):
            with self.subTest(text=text),self.assertRaises((RuntimeError,ValueError)):
                driver.fpga_result(text,'FPGA_PROBE_RESULT')

if __name__=='__main__': unittest.main()
