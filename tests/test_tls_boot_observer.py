"""Only model child-process ownership; never access JTAG, UART or credentials."""
import contextlib,json,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import probe_z7_nano_tls_boot as boot

class ObserverTest(unittest.TestCase):
    def run_observer(self,timeout=False,inner_failure=False):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1]/'tmp') as temp:
            root=Path(temp);folder=root/'output/z7_nano/wifi';folder.mkdir(parents=True)
            (folder/'program.json').write_text(json.dumps({'attempt':'test-program','cable_serial':'test-cable'}))
            class Child:
                pid=12345
                returncode=0
                calls=0
                def poll(self):return None if timeout and self.calls==1 else 0
                def communicate(self,**kwargs):
                    self.calls+=1
                    if timeout and self.calls==1:raise subprocess.TimeoutExpired('xsdb',240)
                    lines=[]
                    for name,phase,error in [('positive',8,0),('positive_close',0,0),('wrong_identity',6,43),
                        ('wrong_identity_close',0,0),('positive_again',8,0),('positive_again_close',0,0)]:
                        words=[0x3154454e,2,phase,error,1,0,0,0,0,0,107,0,0,0,1,1]
                        lines.append('ARM_TLS_RESULT '+name+' '+' '.join(hex(w) for w in words))
                    if inner_failure:
                        self.returncode=1
                        return '\n'.join(lines[:2])+'\nARM_TLS_BOOT_FAILED\nARM_TLS_RELEASED 0\n',None
                    return '\n'.join(lines)+'\nPASS_ARM_TLS_BOOT\nARM_TLS_RELEASED 1\n',None
            def save(path,value):
                path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value))
            with contextlib.ExitStack() as stack:
                for obj,name,value in [(boot.display,'ROOT',root),(boot.display,'OUT',folder),
                    (boot.network,'configure',lambda:None),(boot,'read_credentials_line',lambda:'{"ssid":"unit-test","password":"unit-pass"}'),
                    (boot.display,'execute_app',lambda *args:None),(boot.display,'checked_app',lambda:{'elf_sha256':'test-elf'}),
                    (boot.display,'hashes',lambda _:{}),(boot.display,'save',save),
                    (boot.display.base,'hardware_lock',contextlib.nullcontext),
                    (boot.subprocess,'Popen',lambda *a,**kw:Child()),
                    (boot.subprocess,'run',lambda *a,**kw:subprocess.CompletedProcess(a,0)),
                    (sys,'argv',['probe','--credentials-stdin'])]:stack.enter_context(patch.object(obj,name,value))
                result=boot.main()
            record=json.loads((root/'output/z7_nano/tls_boot/latest.json').read_text())
            failed=timeout or inner_failure
            self.assertEqual(result,1 if failed else 0)
            self.assertEqual(record['released'],not failed)
            self.assertTrue(record['tool_tree_stopped'])
            self.assertEqual((root/'output/z7_nano/tls/uart_uncertain.json').exists(),failed)
            self.assertEqual(record['status'],'failed' if failed else 'passed')
    def test_complete(self):self.run_observer()
    def test_timeout_terminates_owned_tree_and_marks_uart(self):self.run_observer(timeout=True)
    def test_inner_timeout_after_prior_close_is_not_released(self):self.run_observer(inner_failure=True)

if __name__=='__main__':unittest.main()
