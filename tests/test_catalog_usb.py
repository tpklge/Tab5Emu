import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from emulator_usb import catalog_page

class Device:
    def __init__(self, reply): self.reply=reply;self.calls=[]
    def _call(self,*args,**kwargs): self.calls.append((args,kwargs));return self.reply

class CatalogUsbTest(unittest.TestCase):
    def test_full_catalog_request_and_timeout(self):
        c=Device({'total':5000,'offset':4928,'next':4932,'roms':[{}]*4})
        self.assertEqual(catalog_page(c,4928,'gba','ação')['next'],4932)
        self.assertEqual(c.calls[0][0],(0x17,b'4928\0gba\0'+ 'ação'.encode()))
        self.assertEqual(c.calls[0][1]['timeout_s'],25)
    def test_rejects_invalid_requests_before_io(self):
        c=Device({})
        for args in ((-1,'',''),(100001,'',''),(0,'psx',''),(0,'','x\0y'),(0,'','á'*65)):
            with self.assertRaises(ValueError):catalog_page(c,*args)
        self.assertEqual(c.calls,[])
    def test_rejects_a_stalled_response(self):
        c=Device({'total':5000,'next':64,'roms':[]})
        with self.assertRaisesRegex(ValueError,'progresso'):catalog_page(c,64)
    def test_empty_tail_is_terminal(self):
        c=Device({'total':10,'next':1000,'roms':[]})
        self.assertEqual(catalog_page(c,1000)['roms'],[])

if __name__=='__main__':unittest.main()
