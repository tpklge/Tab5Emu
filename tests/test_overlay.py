import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / "tools"))
from overlay_checks import resident_data_references, relocated_code_references
import struct


class OverlayRegression(unittest.TestCase):
    def test_link_provenance_preserves_calls_without_ascii_false_positives(self):
        symbols = [0, 0x48100020, 0x40000000]
        data = b''.join(struct.pack('<IIi',offset,(symbol<<8)|kind,addend) for offset,symbol,kind,addend in (
            (0x40001000,1,18,0), (0x40001008,1,26,4),
            (0x40001010,2,26,0), (0x40001018,1,51,0)))
        self.assertEqual(relocated_code_references(data,symbols,0x48100000,0x48c00000),[(0x40001000,0x48100020),(0x40001008,0x48100024)])
        # LUI/ADDI spelling "UsbH" or "RIGH" has no relocation at all.
        self.assertEqual(relocated_code_references(b'',symbols,0x48100000,0x48c00000),[])
    def test_instruction_bytes_are_not_a_pointer_but_data_is(self):
        # Actual lv_area_diff encoding in this stable link:
        # slt a6,a3,a4 = 0x00e6a833; xori a6,a6,1 = 0x00184813.
        # A four-byte window starting halfway through slt spells 0x481300e6,
        # coincidentally equal to a function start in the smaller overlay.
        instructions = bytes.fromhex("33a8e60013481800")
        crossing_window = instructions[2:6]
        targets = {0x481300e6}
        self.assertEqual(resident_data_references(".tab5_res.text.lv_area_diff", crossing_window, targets), [])
        self.assertEqual(resident_data_references(".tab5_res.rodata.callback", crossing_window, targets), [(0, 0x481300e6)])
        self.assertEqual(resident_data_references(".data.ptr", crossing_window + b"x", targets), [(0, 0x481300e6)])


if __name__ == "__main__":
    unittest.main()
