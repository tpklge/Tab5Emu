import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from sd_core_format import descriptor,verify

class CoreFormatTests(unittest.TestCase):
    def test_fixed_image_and_address_binding(self):
        data=bytes(range(256))*8;address=0x48300000;header=descriptor(data,address)
        self.assertEqual(len(verify(header,data,address)),64)
        for changed in (data[:-1],data+b'x',bytes([data[0]^1])+data[1:]):
            with self.assertRaises(ValueError):verify(header,changed,address)
        with self.assertRaises(ValueError):verify(header,data,address+128)
        with self.assertRaises(ValueError):verify(header[:-1],data,address)
        for offset in (0,8,12,16,47):
            broken=bytearray(header);broken[offset]^=1
            with self.assertRaises(ValueError):verify(broken,data,address)
    def test_bounds(self):
        for data,address in ((b'',0x48300000),(b'x',0x40000000),(b'x',0x50000000),(b'xx',0x49ffffff)):
            with self.assertRaises(ValueError):descriptor(data,address)
    def test_native_assets_opt_into_larger_bounded_payload(self):
        data=b'\0'*(8*1024*1024+128)
        with self.assertRaises(ValueError):descriptor(data,0x48100000)
        header=descriptor(data,0x48100000,16*1024*1024)
        self.assertEqual(len(verify(header,data,0x48100000)),64)
