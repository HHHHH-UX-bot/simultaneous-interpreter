"""Public UART evidence keeps only capability fields, never arbitrary replies."""
import sys
import unittest
import struct
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"scripts"))
from probe_z7_nano_tls_inventory import public_fields

class InventoryFields(unittest.TestCase):
    def test_partition_metadata(self):
        reply='\r\n+SYSFLASH:"server_ca",64,0,0x70000,4096\r\nOK\r\n'
        self.assertEqual(public_fields("partitions",reply),{"partitions":[{
            "name":"server_ca","type":64,"subtype":0,"address":458752,"size":4096}]})
    def test_authentication_mode(self):
        self.assertEqual(public_fields("auth","\r\n+CIPSSLCCONF:0,2,0,0\r\nOK\r\n"),
                         {"numeric_arguments":[0,2,0,0]})
    def test_arbitrary_text_is_not_evidence(self):
        self.assertEqual(public_fields("auth","unrelated-private-text\r\nERROR\r\n"),{})
        self.assertEqual(public_fields("sni",'+CIPSSLCSNI:0,"private-value"\r\n'),
                         {"has_parameter_line":True})
    def test_version_fields_only(self):
        value=public_fields("version","AT version:2.2.2.0\r\nSDK version:v3.4\r\nother-field\r\nOK\r\n")
        self.assertEqual(value["at_version"],"2.2.2.0")
        self.assertEqual(value["sdk_version"],"v3.4")
        self.assertEqual(set(value),{"at_version","sdk_version","response_sha256"})
    def test_ca_header_is_binary_and_bounded(self):
        header=struct.pack("<HHIBBH",0xf1f1,1,1204,1,0,1200)
        value=public_fields("ca_header",(b"\r\n+SYSFLASH:12,"+header+b"\r\nOK\r\n").decode("latin1"))
        self.assertEqual(value,{"magic":0xf1f1,"item_count":1,"items_bytes":1204,
                                "first_type":1,"first_id":0,"first_bytes":1200})

if __name__=="__main__":unittest.main()
