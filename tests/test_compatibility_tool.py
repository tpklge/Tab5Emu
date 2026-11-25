import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
from smoke_compatibility import check_rtc_snapshot


class RtcProbeTest(unittest.TestCase):
    def test_preserved_day_is_valid(self):
        check_rtc_snapshot((0x32544352,1,1,2,15,6,0,1,123), bytes([15,2,1,1,128]))

    def test_latch_can_precede_next_second_at_day_wrap(self):
        check_rtc_snapshot((0x32544352,0,0,0,0,0,0,1,123), bytes([59,59,23,255,129]))

    def test_wrong_day_or_missing_carry_is_refused(self):
        clock=(0x32544352,1,1,2,15,6,0,1,123)
        for latched in (bytes([15,2,1,0,128]), bytes([15,2,1,1,0])):
            with self.assertRaises(AssertionError): check_rtc_snapshot(clock,latched)


if __name__=='__main__': unittest.main()
