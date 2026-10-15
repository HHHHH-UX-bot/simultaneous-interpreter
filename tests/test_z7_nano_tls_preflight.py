"""Decode real certificate metadata without exporting unrelated PKI objects."""
import sys,struct,unittest,tkinter
from datetime import datetime,timedelta,timezone
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from probe_z7_nano_tls import certificate_metadata
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec

class PublicPKI(unittest.TestCase):
    def test_certificate_only_not_private_key(self):
        key=ec.generate_private_key(ec.SECP256R1());now=datetime.now(timezone.utc)
        name=x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME,"test public CA")])
        cert=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(1).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=1)).sign(key,hashes.SHA256()).public_bytes(serialization.Encoding.DER)
        def item(kind,data):return struct.pack("<BBH",kind,0,len(data))+data+b"\xff"*((-len(data))%4)
        items=item(1,cert)+item(3,b"private-payload-should-never-appear")
        value=certificate_metadata(struct.pack("<HHI",0xf1f1,2,len(items))+items)
        self.assertEqual(len(value),1);self.assertEqual(value[0]["subject"],"CN=test public CA")
        self.assertNotIn("private-payload",str(value))
    def test_truncated_partition_is_rejected(self):
        with self.assertRaises(ValueError):certificate_metadata(struct.pack("<HHI",0xf1f1,1,1200))
        with self.assertRaises(ValueError):certificate_metadata(struct.pack("<HHI",0xf1f1,1,0)+b"\x03\x00\x00\x00")

class PreflightControl(unittest.TestCase):
    def simulate(self,failure="",script_name="z7_nano_tls_preflight.tcl"):
        t=tkinter.Tcl();t.setvar("argv",("board","cloud.example"));t.setvar("fail_at",failure)
        t.eval('''
            set commands {};set markers {}
            proc transact {name command} {
                lappend ::commands [list $name $command]
                if {$name eq $::fail_at} {return "\\r\\nERROR\\r\\n"}
                switch -- $name {
                    version {return "AT version:test\\r\\nOK\\r\\n"}
                    store {return "+SYSSTORE:0\\r\\nOK\\r\\n"}
                    time {return "+CIPSNTPTIME:[clock format [clock seconds] -format {%a %b %e %H:%M:%S %Y} -gmt 1]\\r\\nOK\\r\\n"}
                    auth - auth_before {return "+CIPSSLCCONF:0,2,0,0\\r\\nOK\\r\\n"}
                    tls_wrong_identity {return "\\r\\nERROR\\r\\n"}
                    default {return "\\r\\nOK\\r\\n"}
                }
            }
            rename gets original_gets
            proc gets {channel args} {
                if {$channel eq "stdin"} {return [binary encode hex {AT+CWJAP="public","test-password"}]}
                return [uplevel 1 [linsert $args 0 original_gets $channel]]
            }
            proc mrd {args} {return 0}
            proc disconnect {} {}
            proc puts {message} {lappend ::markers $message}
        ''')
        script=(Path(__file__).resolve().parents[1]/"scripts"/script_name).read_text()
        script=script.replace('source [file join [file dirname [info script]] z7_nano_tls_uart.tcl]','')
        t.eval(script)
        return t,t.splitlist(t.getvar("commands")),t.splitlist(t.getvar("markers"))
    def test_pre_store_failure_does_not_join_or_cleanup_settings(self):
        t,commands,markers=self.simulate("store")
        names=[t.splitlist(c)[0] for c in commands]
        self.assertNotIn("join",names);self.assertNotIn("cleanup_name",names)
        self.assertNotIn("PASS_Z7_TLS_PREFLIGHT",markers)
    def test_tls_failure_does_not_run_negative_or_claim_pass(self):
        t,commands,markers=self.simulate("tls_positive")
        self.assertNotIn("wrong_name",[t.splitlist(c)[0] for c in commands])
        self.assertIn("TLS_PREFLIGHT_STOP tls_positive",markers)
        self.assertNotIn("PASS_Z7_TLS_PREFLIGHT",markers)
    def test_positive_negative_positive_and_single_join_terminator(self):
        t,commands,markers=self.simulate()
        joined=[t.splitlist(c)[1] for c in commands if t.splitlist(c)[0]=="join"]
        self.assertEqual(len(joined),1);self.assertTrue(joined[0].endswith("\r\n"))
        self.assertFalse(joined[0].endswith("\r\n\r\n"))
        self.assertIn("PASS_Z7_TLS_PREFLIGHT",markers)
    def test_transport_restores_authentication_after_connect_error(self):
        t,commands,markers=self.simulate("tls_noauth","z7_nano_tls_transport.tcl")
        self.assertIn("PASS_Z7_TLS_TRANSPORT",markers)
        self.assertIn(("restore_auth","AT+CIPSSLCCONF=2,0,0\r\n"),[t.splitlist(c) for c in commands])
    def test_transport_does_not_disable_auth_before_store_check(self):
        t,commands,markers=self.simulate("store","z7_nano_tls_transport.tcl")
        self.assertNotIn("noauth_set",[t.splitlist(c)[0] for c in commands])
        self.assertNotIn("PASS_Z7_TLS_TRANSPORT",markers)
    def test_transport_close_failure_still_restores_auth_but_does_not_pass(self):
        t,commands,markers=self.simulate("cleanup_close","z7_nano_tls_transport.tcl")
        self.assertNotIn("PASS_Z7_TLS_TRANSPORT",markers)
        self.assertIn("restore_auth",[t.splitlist(c)[0] for c in commands])

class BinaryATRead(unittest.TestCase):
    def test_internal_ok_line_does_not_truncate_certificate(self):
        t=tkinter.Tcl()
        source=(Path(__file__).resolve().parents[1]/"scripts/z7_nano_tls_uart.tcl").read_text()
        t.eval(source[source.index("proc take_rx {}"):])
        content=b"prefix\r\nOK\r\n"+b"x"*500
        self.assertEqual(len(content),512)
        t.setvar("parts",("",(b"\r\n+SYSFLASH:512,"+content[:32]).decode("latin1"),
                           (content[32:]+b"\r\nOK\r\n").decode("latin1")))
        t.eval('''
            proc take_rx {} {set value [lindex $::parts 0];set ::parts [lrange $::parts 1 end];return $value}
            proc mrd {args} {if {[lindex $args end] eq "0x43c20004"} {return 2};return 0}
            proc mwr {args} {}
            proc puts {args} {}
            proc flush {args} {}
        ''')
        reply=t.call("transact","ca_chunk0",'AT+SYSFLASH=2,"client_ca",0,512\r\n')
        self.assertEqual(reply if isinstance(reply,bytes) else reply.encode("latin1"),b"\r\n+SYSFLASH:512,"+content+b"\r\nOK\r\n")
        self.assertFalse(t.splitlist(t.getvar("parts")))

if __name__=="__main__":unittest.main()
