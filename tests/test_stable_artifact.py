import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
from check_stable_artifact import check
from stage_gb_loader import stage
from stage_audio_policy import stage as stage_audio


class StableGateTest(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)
        (self.root/'CMakeCache.txt').write_text('TAB5_PROFILE:STRING=stable\n')
        self.commands=[{'file':'/repo/components/gba/src/gba.cpp','command':'c++ -DENABLE_GBA -c gba.cpp'}]
        self.write_commands();self.write_image(4010000)

    def write_commands(self):
        (self.root/'compile_commands.json').write_text(json.dumps(self.commands))

    def write_image(self,size):
        data=bytearray(size-32);data[0]=0xe9;data[12]=0x12;data[23]=1;data[80:88]=b'tab5-emu'
        (self.root/'tab5-emu.bin').write_bytes(data+hashlib.sha256(data).digest())

    def test_accepts_only_build_evidence_without_claiming_hardware_test(self):
        report=check(self.root)
        self.assertTrue(report['app_only']);self.assertFalse(report['jit_compiled'])
        self.assertFalse(report['hardware_tested'])

    def test_rejects_growth_and_shrink_of_reservation(self):
        for size in (3900000,4070000):
            self.write_image(size)
            with self.assertRaisesRegex(ValueError,'reservation'):check(self.root)

    def test_rejects_experimental_code_and_flags(self):
        for flag in ('-DHAVE_DYNAREC','-DTAB5_GBA_RV32','-DTAB5_GBA_HEAP_CHECK'):
            self.commands[0]['command']='c++ '+flag;self.write_commands()
            with self.assertRaisesRegex(ValueError,'experimental'):check(self.root)
        self.commands[0]={'file':'/repo/components/gba/rv32/platform.c','arguments':['cc','-c','platform.c']}
        self.write_commands()
        with self.assertRaisesRegex(ValueError,'experimental'):check(self.root)

    def test_rejects_other_profile(self):
        (self.root/'CMakeCache.txt').write_text('TAB5_PROFILE:STRING=gba-rv32\n')
        with self.assertRaisesRegex(ValueError,'stable profile'):check(self.root)

class GbLoaderStageTest(unittest.TestCase):
    def test_pinned_loader_gets_sram_failure_guard(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'loader.c'
            stage(Path(__file__).parents[1]/'components/gbc/gnuboy/gnuboy/src/loader.c',output)
            self.assertIn('if (!sram_ptr) { ram.sbank = NULL; return -1; }',output.read_text())

    def test_unreviewed_source_cannot_replace_adapter(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'upstream.c';source.write_text('changed')
            output=Path(folder)/'loader.c';output.write_text('previous audited adapter')
            with self.assertRaises(ValueError):stage(source,output)
            self.assertEqual(output.read_text(),'previous audited adapter')


class AudioStageTest(unittest.TestCase):
    def test_real_pinned_feeder_keeps_ring_and_uses_one_frame_wait(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)/'i2s_tx_feed.hpp'
            stage_audio(Path(__file__).parents[1]/'managed_components/espp__codec/include/i2s_tx_feed.hpp',output)
            self.assertIn('WHOLE_BLOCK_WAIT_MS = 16',output.read_text())
            self.assertIn('DMA_DESC_NUM = 16',output.read_text())
    def test_drift_cannot_replace_the_previous_generated_header(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'upstream.hpp';source.write_text('changed')
            output=Path(folder)/'feeder.hpp';output.write_text('previous audited feeder')
            with self.assertRaises(ValueError):stage_audio(source,output)
            self.assertEqual(output.read_text(),'previous audited feeder')

if __name__=='__main__':unittest.main()
