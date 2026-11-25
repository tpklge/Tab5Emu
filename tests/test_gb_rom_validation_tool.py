"""The host probe must protect fixture identity and restore it on failure."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
import smoke_gb_rom_validation as probe


class ProbeTest(unittest.TestCase):
    def setUp(self):
        test = self
        self.original = probe.gameboy()
        self.writes = []
        self.changes = []

        class Control:
            def info(self): return {'elf_sha256': 'expected'}
            def status(self): return {'state': 'menu', 'uptime_ms': 100}
            def read_file(self, path): return test.writes[-1] if test.writes else test.original
            def write_file(self, path, data): test.writes.append(data)
            def settings(self, change=None):
                if change: test.changes.append(change)
                return {'settings': {'resume': 1}}
            def launch(self, *args, **kwargs): raise RuntimeError('launch failed')

        self.control = Control()

    def test_unexpected_firmware_prevents_writes(self):
        with self.assertRaises(ValueError): probe.run(self.control, 'different', {})
        self.assertEqual(self.writes, [])
        self.assertEqual(self.changes, [])

    def test_exception_restores_exact_rom_and_resume_preference(self):
        report = {}
        with self.assertRaisesRegex(RuntimeError, 'launch failed'):
            probe.run(self.control, 'expected', report)
        self.assertEqual(self.writes, [self.original[:64], self.original])
        self.assertEqual(self.changes, ['resume=0', 'resume=1'])
        self.assertTrue(report['original_rom_restored'])
        self.assertFalse(report['complete'])

    def test_cleanup_error_keeps_primary_exception(self):
        def write(path, data):
            if data == self.original: raise OSError('restore disconnected')
            self.writes.append(data)
        self.control.write_file = write
        report = {}
        with self.assertRaisesRegex(RuntimeError, 'launch failed'):
            probe.run(self.control, 'expected', report)
        self.assertIn('restore disconnected', report['cleanup_error'])
        self.assertIn('launch failed', report['error'])
        self.assertFalse(report['complete'])


if __name__ == '__main__': unittest.main()
