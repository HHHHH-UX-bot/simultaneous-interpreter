"""Fault-injection checks for shared hardware provenance/process gates; no board."""
import argparse,json,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch

PROJECT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(PROJECT/'scripts'))
import ui_debug as ui

class DebugGates(unittest.TestCase):
    def setUp(self):
        self.root=Path(tempfile.mkdtemp(prefix='ui_gate_',dir=PROJECT/'tmp/subtitle_ui'))
        self.fb=self.root/'output/framebuffer';self.fb.mkdir(parents=True)
        self.out=self.root/'output/subtitle_ui';self.out.mkdir()
        (self.root/'tmp/subtitle_ui').mkdir(parents=True)
        attempt=self.fb/'attempts/run1';attempt.mkdir(parents=True)
        (attempt/'framebuffer_app.elf').write_bytes(b'test UI elf')
        (attempt/'framebuffer_top.bit').write_bytes(b'test framebuffer bit')
        self.elf=ui.fb.sha(attempt/'framebuffer_app.elf');self.bit=ui.fb.sha(attempt/'framebuffer_top.bit')
        self.prior={'Status':'passed','ELFSHA256':self.elf,'BitstreamSHA256':self.bit,
                    'ProgramAttempt':'program1','SnapshotHashes':{'framebuffer_app.elf':self.elf,'framebuffer_top.bit':self.bit}}
        self.save('attempts/run1/result.json',self.prior)
        self.save('active_app.json',{'Status':'ready','RunAttempt':'run1','ProgramAttempt':'program1'})
        self.save('program.json',{'Status':'passed','Attempt':'program1','BitstreamSHA256':self.bit})
        self.patches=[patch.object(ui,'ROOT',self.root),patch.object(ui,'OUT',self.out),
                      patch.object(ui.fb,'ROOT',self.root),patch.object(ui.fb,'OUT',self.fb),
                      patch.object(ui.fb,'PS_FAILURE_STATE',self.root/'ps_failure.json'),
                      patch.object(ui.app,'OUT',self.out),patch.object(ui.runner,'OUT',self.out),
                      patch.object(ui.runner,'TEMP',self.root/'tmp/subtitle_ui'),
                      patch.object(ui.app,'checked_ui_build',return_value={'ELFSHA256':self.elf,'ModelBytes':11392}),
                      patch.object(ui.fb,'checked_build',return_value={'BitstreamSHA256':self.bit}),
                      patch.object(ui.fb,'hashes',return_value={}),
                      patch.object(ui.fb,'probe_live',return_value={'DONE':1,'EOS':1,'CRCError':0,'USRAccess':0x46564231})]
        self.mocks=[p.start() for p in self.patches]
        for p in self.patches: self.addCleanup(p.stop)
        self.args=argparse.Namespace(action='sample',xsdb=Path('fake-xsdb'),vivado=Path('fake-vivado'),
                                     command=None,expected_epoch=None)
    def save(self,path,data): (self.fb/path).write_text(json.dumps(data),encoding='utf-8')
    def test_changed_bit_rejected_before_any_probe(self):
        self.prior['BitstreamSHA256']='wrong';self.save('attempts/run1/result.json',self.prior)
        with self.assertRaisesRegex(RuntimeError,'current simulated UI'): ui.execute(self.args)
        ui.fb.probe_live.assert_not_called()
    def test_wrong_program_token_rejected(self):
        self.save('program.json',{'Status':'passed','Attempt':'different','BitstreamSHA256':self.bit})
        with self.assertRaisesRegex(RuntimeError,'current simulated UI'): ui.execute(self.args)
        ui.fb.probe_live.assert_not_called()
    def test_modified_run_snapshot_rejected(self):
        (self.fb/'attempts/run1/framebuffer_app.elf').write_bytes(b'changed after launch')
        with self.assertRaisesRegex(RuntimeError,'snapshot has changed'): ui.execute(self.args)
        ui.fb.probe_live.assert_not_called()
    def test_surviving_sample_tree_closes_shared_gate(self):
        def failed(*args,**kwargs):
            kwargs['on_start'](99123)
            raise ui.runner.StageError('Injected sample timeout',False,99123)
        with patch.object(ui.runner,'command',side_effect=failed),patch.object(ui.runner,'cleanup') as cleanup:
            self.assertEqual(ui.execute(self.args),1);cleanup.assert_not_called()
        failure=json.loads((self.fb/'failure_state.json').read_text())
        self.assertFalse(failure['OwnTreeStopped']);self.assertEqual(failure['CPUState'],'unknown')
        self.assertEqual(failure['OriginalProcessID'],99123)
        with self.assertRaisesRegex(RuntimeError,'Unconfirmed hardware'): ui.fb.hardware_gate()
        self.assertNotEqual(json.loads((self.fb/'active_app.json').read_text())['Status'],'ready')
    def test_exited_failed_capture_requires_confirmed_cleanup(self):
        self.args.action='capture'
        def failed(*args,**kwargs):
            kwargs['on_start'](99124)
            raise ui.runner.StageError('Injected capture failure',True,99124)
        with patch.object(ui.runner,'command',side_effect=failed),patch.object(ui.runner,'cleanup',
                return_value={'CPUState':'confirmed_stopped','OwnTreeStopped':True}) as cleanup:
            self.assertEqual(ui.execute(self.args),1);cleanup.assert_called_once()
        ui.fb.hardware_gate()
        self.assertEqual(json.loads((self.fb/'failure_state.json').read_text())['CPUState'],'confirmed_stopped')
    def test_successful_sample_reopens_gate_only_with_checked_report(self):
        text=('UI_RESULT 0x55493131 2 0x1b00000 11456 11392 0 5000 20 10 10 1 1 1 0 0 3\n'
              'DISPLAY_RESULT 0x46504231 0x600d600d 2 3 10 0 300 0 0 0 614400 0 0 300 0 0\n')
        def passed(*args,**kwargs): kwargs['on_start'](99125);return text
        with patch.object(ui.runner,'command',side_effect=passed): self.assertEqual(ui.execute(self.args),0)
        ui.fb.hardware_gate()
        state=json.loads((self.fb/'failure_state.json').read_text())
        self.assertEqual(state['CPUState'],'running_animation');self.assertTrue(state['OwnTreeStopped'])

if __name__=='__main__': unittest.main()
