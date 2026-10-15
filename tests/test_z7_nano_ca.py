"""CA writes are limited to public PKI, with an explicitly pinned trust root."""
import sys,struct,unittest
from datetime import datetime,timedelta,timezone
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from provision_z7_nano_ca import public_image,candidate_image
from cryptography import x509
from cryptography.hazmat.primitives import hashes,serialization
from cryptography.hazmat.primitives.asymmetric import ec

class PublicCAImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        key=ec.generate_private_key(ec.SECP256R1());now=datetime.now(timezone.utc)
        name=x509.Name([x509.NameAttribute(x509.oid.NameOID.COMMON_NAME,"public fixture")])
        cls.pem=x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key()).serial_number(1).not_valid_before(now-timedelta(days=1)).not_valid_after(now+timedelta(days=1)).sign(key,hashes.SHA256()).public_bytes(serialization.Encoding.PEM)
    def image(self,kind=1,tail=b""):
        item=struct.pack("<BBH",kind,0,len(self.pem))+self.pem+b"\xff"*((-len(self.pem))%4)
        return struct.pack("<HHI",0xf1f1,1,len(item))+item+tail
    def test_public_certificate_and_erased_padding(self):
        self.assertEqual(public_image(self.image(tail=b"\xff"*16))[0]["subject"],"CN=public fixture")
    def test_private_key_object_is_rejected(self):
        with self.assertRaises(ValueError):public_image(self.image(kind=3))
    def test_unexplained_tail_is_rejected(self):
        with self.assertRaises(ValueError):public_image(self.image(tail=b"unexplained bytes"))
    def test_candidate_rejects_unpinned_certificate(self):
        with self.assertRaises(ValueError):candidate_image(self.pem)
    def test_payload_after_pem_is_rejected(self):
        content=self.pem+b"non-certificate-payload"
        item=struct.pack("<BBH",1,0,len(content))+content+b"\xff"*((-len(content))%4)
        with self.assertRaises(ValueError):public_image(struct.pack("<HHI",0xf1f1,1,len(item))+item)
    def test_repository_root_builds_one_aligned_ca(self):
        pem=(Path(__file__).resolve().parents[1]/"hardware/esp8266/globalsign_r3.pem").read_bytes()
        image=candidate_image(pem)
        self.assertEqual(len(image)%4,0);self.assertLessEqual(len(image),8192)
        self.assertEqual(len(public_image(image)),1);self.assertEqual(public_image(image)[0]["type"],1)

if __name__=="__main__":unittest.main()
