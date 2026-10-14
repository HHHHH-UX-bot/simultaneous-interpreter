"""Isolated directory/command fixtures; no tools or devices are invoked."""
import importlib,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import run_ps_wifi as net
import run_framebuffer as fb
import run_ps_bringup as runner
class Gates(unittest.TestCase):
    def setUp(self):
        importlib.reload(runner);importlib.reload(fb);importlib.reload(net)
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
        self.base=self.root/'output/framebuffer';self.out=self.root/'output/ps_wifi'
        self.base.mkdir(parents=True);self.out.mkdir(parents=True)
        net.ROOT=fb.ROOT=runner.ROOT=self.root;net.BASE_OUT=self.base;net.OUT=self.out
        fb.PS_FAILURE_STATE=self.root/'output/ps_bringup/failure_state.json'
        self.original_gate=fb.hardware_gate
    def tearDown(self):self.temp.cleanup();importlib.reload(runner);importlib.reload(fb);importlib.reload(net)
    def live(self,identity=0x46574e31,done=1):
        return dict(DONE=done,EOS=done,CRCError=0,USRAccess=identity)
    def test_failure_is_shared_and_old_gate_rejects(self):
        net.configure('sample');fb.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False})
        for directory in (self.out,self.base,self.root/'output/ps_bringup'):
            self.assertEqual(json.loads((directory/'failure_state.json').read_text())['CPUState'],'unknown')
        fb.OUT=self.base
        with self.assertRaises(RuntimeError):self.original_gate()
    def test_successful_cleanup_clears_every_gate(self):
        net.configure('cleanup');fb.save('failure_state.json',{'CPUState':'unknown','OwnTreeStopped':False})
        fb.save('failure_state.json',{'CPUState':'confirmed_stopped','OwnTreeStopped':True});fb.hardware_gate()
    def test_surviving_probe_blocks_old_entry(self):
        net.configure('probe');fb.save('probe_operation.json',{'OwnTreeStopped':False});fb.OUT=self.base
        with self.assertRaises(RuntimeError):self.original_gate()
        self.assertFalse(json.loads((self.root/'output/ps_bringup/failure_state.json').read_text())['OwnTreeStopped'])
    def test_old_ui_invalidated_not_relabelled_ready(self):
        net.configure('run');fb.save('active_app.json',{'Status':'ready','RunAttempt':'new'})
        self.assertEqual(json.loads((self.base/'active_app.json').read_text())['Status'],'invalidated_by_ps_wifi')
    def test_old_id_allowed_only_before_known_program(self):
        with patch.object(fb,'probe_live',return_value=self.live(0x46564231)):
            net.configure('program');fb.probe_live(None,'x');fb.require_framebuffer(self.live(0x46564231))
            with self.assertRaises(RuntimeError):fb.require_framebuffer(self.live(0x46564231))
            fb.require_framebuffer(self.live())
    def test_done_zero_still_requires_new_id_after_program(self):
        with patch.object(fb,'probe_live',return_value=self.live(0,0)):
            net.configure('program');fb.probe_live(None,'x')
            with self.assertRaises(RuntimeError):fb.require_framebuffer(self.live(0x46564231))
    def test_run_never_accepts_old_id(self):
        with patch.object(fb,'probe_live',return_value=self.live(0x46564231)):
            net.configure('run');fb.probe_live(None,'x')
            with self.assertRaises(RuntimeError):fb.require_framebuffer(self.live(0x46564231))
    def test_programmer_rerouted_to_strict_new_identity(self):
        with patch.object(runner,'command',return_value='ok') as cmd:
            net.configure('program');fb.runner.command([self.root/'scripts/hardware_framebuffer.tcl','bit'])
            self.assertEqual(cmd.call_args.args[0][0],self.root/'scripts/hardware_ps_wifi.tcl')
    def test_sample_gate_exists_before_process_launch_and_restores_after_exit(self):
        def fake(args,*pos,**kwargs):
            for directory in (self.out,self.base,self.root/'output/ps_bringup'):
                self.assertFalse(json.loads((directory/'failure_state.json').read_text())['OwnTreeStopped'])
            kwargs['on_start'](123);return 'ok'
        with patch.object(runner,'command',side_effect=fake):
            net.configure('sample');fb.runner.command(['xsdb',self.root/'scripts/ps_framebuffer.tcl','sample'])
            fb.hardware_gate()
    def test_cleanup_cannot_unlock_old_unconfirmed_process(self):
        (self.base/'failure_state.json').write_text(json.dumps({'CPUState':'unknown','OwnTreeStopped':False}))
        (self.out/'failure_state.json').write_text(json.dumps({'CPUState':'confirmed_stopped','OwnTreeStopped':True}))
        with self.assertRaises(RuntimeError):net.configure('cleanup')
        self.assertFalse(json.loads((self.base/'failure_state.json').read_text())['OwnTreeStopped'])
if __name__=='__main__':unittest.main()
