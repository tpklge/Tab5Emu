"""Simulated USB failures: report retention and restoration, not hardware validation."""
import hashlib
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parents[1] / 'tools'))
import probe_gba_lifecycle as probe


class LifecycleTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        manifest = root / 'rom.json'
        manifest.write_text(json.dumps({'sha256': hashlib.sha256(b'ROM').hexdigest()}))
        self.args = types.SimpleNamespace(output=root / 'result.json', manifest=manifest,
                                         elf_sha256='a' * 64, serial='test', rom='fixture.gba',
                                         scene=[], cycles=1, seconds=12)
        self.changes = []
        test = self

        class Link:
            def __init__(self, serial): pass
            def open(self, **kwargs): return self
            def __enter__(self): return self
            def __exit__(self, *args): pass

        class Control:
            state = 'menu'
            ticks = 1
            def __init__(self, link): pass
            def info(self): return {'elf_sha256': test.args.elf_sha256}
            def status(self):
                self.ticks += 1
                return {'state': self.state, 'uptime_ms': self.ticks,
                        'game_frames': 720, 'paused': False}
            def settings(self, change=None):
                if change:
                    test.changes.append(change)
                    return {}
                return {'settings': {'volume': 23, 'resume': 1, 'input_test': 0},
                        'gba_cpu_requested': 0, 'gba_metrics': {'backend': 'gpsp-rv32'}}
            def read_file(self, path):
                if path == 'fixture.gba': return b'ROM'
                return b'T5RV' + (240).to_bytes(4, 'little') + (4368456).to_bytes(4, 'little') + bytes(4)
            def launch(self, rom): self.state = 'game'
            def quit(self): self.state = 'menu'
            def buttons(self, mask): pass

        self.control = Control
        modules = {'emulink.link': types.SimpleNamespace(Link=Link),
                   'emulink.control': types.SimpleNamespace(Control=Control),
                   'espp_coredump.client': types.SimpleNamespace(
                       CoreDumpClient=lambda link: types.SimpleNamespace(size=lambda: 0))}
        for context in (patch.dict(sys.modules, modules), patch.object(probe, 'prepare'),
                        patch.object(probe.time, 'sleep')):
            context.start()
            self.addCleanup(context.stop)

    def test_success_restores_original_preferences(self):
        result = probe.run(self.args)
        self.assertTrue(result['complete'])
        self.assertTrue(result['settings_restored'])
        self.assertEqual(self.changes[-4:], ['gba_cpu=0', 'resume=1', 'input_test=0', 'volume=23'])
        self.assertTrue(result['cycles'][0]['complete'])

    def test_disconnect_during_quit_preserves_primary_error_and_partial_results(self):
        def fail_quit(control): raise RuntimeError('quit reset the device')
        def fail_cleanup(control, mask): raise OSError('USB disconnected')
        self.control.quit = fail_quit
        self.control.buttons = fail_cleanup
        with self.assertRaisesRegex(RuntimeError, 'quit reset the device'):
            probe.run(self.args)
        result = json.loads(self.args.output.read_text())
        self.assertFalse(result['complete'])
        self.assertIn('quit reset the device', result['error'])
        self.assertIn('USB disconnected', result['cleanup_error'])
        self.assertIn('alu_before_quit', result['cycles'][0])

    def test_unexpected_firmware_does_not_change_preferences(self):
        self.control.info = lambda control: {'elf_sha256': 'b' * 64}
        with self.assertRaisesRegex(RuntimeError, 'unexpected firmware'):
            probe.run(self.args)
        self.assertEqual(self.changes, [])
        self.assertFalse(json.loads(self.args.output.read_text())['complete'])

    def test_report_cannot_replace_previous_evidence(self):
        self.args.output.write_text('original evidence')
        with self.assertRaises(FileExistsError): probe.run(self.args)
        self.assertEqual(self.args.output.read_text(), 'original evidence')


if __name__ == '__main__':
    unittest.main()
