"""Host failure-path checks only: never connects to a real board."""
import importlib.util
from pathlib import Path
import json
import subprocess
import sys
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('ps_driver',ROOT/'scripts/run_ps_bringup.py')
driver=importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)

class PSDriverFailures(unittest.TestCase):
    def setUp(self):
        self.out=ROOT/'tmp/ps/host_tests'
        self.out.mkdir(parents=True,exist_ok=True)
        self.redirect=patch.object(driver,'OUT',self.out)
        self.redirect.start()

    def tearDown(self): self.redirect.stop()

    def test_timeout_keeps_output_and_kills_only_own_tree(self):
        process=Mock(pid=27182)
        process.communicate.side_effect=[subprocess.TimeoutExpired(['xsdb'],1,output=b'PS_INITIALIZATION_BEGIN\n'),('PS_INITIALIZATION_BEGIN\n',None)]
        with patch.object(driver.subprocess,'Popen',return_value=process),patch.object(driver.subprocess,'run',return_value=Mock(returncode=0,stdout='Killed own tree',stderr='')) as kill:
            with self.assertRaises(driver.StageError) as caught:
                driver.command(['xsdb'],'timeout.log',timeout=1)
        self.assertTrue(caught.exception.own_tree_stopped)
        self.assertEqual(kill.call_args.args[0],['taskkill','/PID','27182','/T','/F'])
        log=(self.out/'logs/timeout.log').read_text(encoding='utf-8')
        self.assertIn('PS_INITIALIZATION_BEGIN',log)
        self.assertIn('HOST_TIMEOUT',log)

    def test_unconfirmed_tree_is_explicit(self):
        process=Mock(pid=27183)
        process.communicate.side_effect=[subprocess.TimeoutExpired(['xsdb'],1,output=b'partial'),subprocess.TimeoutExpired(['xsdb'],10)]
        with patch.object(driver.subprocess,'Popen',return_value=process),patch.object(driver.subprocess,'run',return_value=Mock(returncode=1,stdout='',stderr='not terminated')):
            with self.assertRaises(driver.StageError) as caught:
                driver.command(['xsdb'],'unconfirmed.log',timeout=1)
        self.assertFalse(caught.exception.own_tree_stopped)
        self.assertIn('partial',(self.out/'logs/unconfirmed.log').read_text())

    def test_error_output_cannot_become_success(self):
        process=Mock(returncode=0)
        process.communicate.return_value=('ERROR_PS_BRINGUP: failed\nPASS_PS_DDR_RUN',None)
        with patch.object(driver.subprocess,'Popen',return_value=process):
            with self.assertRaises(driver.StageError):
                driver.command(['xsdb'],'error.log','PASS_PS_DDR_RUN')

    def test_cleanup_requires_confirmation_marker(self):
        process=Mock(returncode=0)
        process.communicate.return_value=('CPU0 only',None)
        with patch.object(driver.subprocess,'Popen',return_value=process):
            self.assertEqual(driver.cleanup('xsdb','failed_attempt')['CPUState'],'unknown')

    def test_keyboard_cancel_terminates_own_tree(self):
        process=Mock(pid=27184)
        process.communicate.side_effect=[KeyboardInterrupt(),('captured before cancel',None)]
        with patch.object(driver.subprocess,'Popen',return_value=process),patch.object(driver.subprocess,'run',return_value=Mock(returncode=0,stdout='Killed own tree',stderr='')):
            with self.assertRaises(driver.StageError) as caught:
                driver.command(['xsdb'],'cancel.log',timeout=1)
        self.assertTrue(caught.exception.own_tree_stopped)
        self.assertEqual(caught.exception.process_id,27184)
        self.assertIn('HOST_CANCELLED',(self.out/'logs/cancel.log').read_text())

    def test_attempt_record_parent_created(self):
        driver.save('attempts/nested/result.json',{'Status':'failed'})
        self.assertTrue((self.out/'attempts/nested/result.json').is_file())

    def test_cleanup_cannot_unlock_an_unconfirmed_execution_tree(self):
        previous={'CPUState':'unknown','OwnTreeStopped':False,'OriginalProcessID':27185}
        driver.save('failure_state.json',previous)
        with patch.object(sys,'argv',['driver','cleanup','--xsdb','not_a_real_tool']),patch.object(driver.subprocess,'Popen') as launch:
            self.assertEqual(driver.execute(),1)
        launch.assert_not_called()
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text()),previous)

    def test_run_refuses_dirty_gate_even_when_cpus_were_stopped(self):
        previous={'CPUState':'confirmed_stopped','OwnTreeStopped':False,'OriginalProcessID':27185}
        driver.save('failure_state.json',previous)
        elf=self.out/'ps_bringup.elf'
        elf.write_bytes(b'host fixture, never executable')
        driver.save('build.json',{'Status':'passed','SourceHashes':driver.hashes(),'ELFSHA256':driver.sha(elf)})
        with patch.object(sys,'argv',['driver','run','--xsdb','not_a_real_tool']),patch.object(driver.subprocess,'Popen') as launch:
            self.assertEqual(driver.execute(),1)
        launch.assert_not_called()
        self.assertEqual(json.loads((self.out/'failure_state.json').read_text()),previous)

if __name__=='__main__': unittest.main()
