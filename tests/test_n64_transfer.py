import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import sync_n64_roms as sync


def rom(marker=0):
    data=bytearray(8192);data[:4]=bytes.fromhex('80371240');data[4096]=marker
    return bytes(data)


class N64TransferTests(unittest.TestCase):
    def setup_dirs(self,folder):
        source=Path(folder)/'source';root=Path(folder)/'Tab5-Emu'
        source.mkdir();root.mkdir();return source,root

    def test_dedup_copy_readback_originals_and_catalog(self):
        with tempfile.TemporaryDirectory() as folder:
            source,root=self.setup_dirs(folder);data=rom()
            (source/'Own game.z64').write_bytes(data);(source/'Own game.z64 2').write_bytes(data)
            (source/'.DS_Store').write_bytes(b'finder')
            report=sync.plan(source,root)
            self.assertEqual(report['unique_games'],1);self.assertEqual(report['source_aliases'],2)
            sync.transfer(report)
            self.assertFalse((root/'roms').exists())
            sync.transfer(report,True)
            self.assertTrue(report['complete']);self.assertTrue(report['games'][0]['readback_verified'])
            self.assertEqual((root/'roms/n64/Own game.z64').read_bytes(),data)
            self.assertEqual((source/'Own game.z64').read_bytes(),data)
            catalog=json.loads((root/'library/games.json').read_text())
            self.assertEqual(catalog['games'][0]['system'],'n64')
            self.assertEqual(report['index']['games'],1)
            again=sync.plan(source,root)
            self.assertEqual(again['games'][0]['action'],'verified_existing')

    def test_conflicting_existing_rom_is_preserved(self):
        with tempfile.TemporaryDirectory() as folder:
            source,root=self.setup_dirs(folder);(source/'Own.z64').write_bytes(rom(1))
            dest=root/'roms/n64/Own.z64';dest.parent.mkdir(parents=True);dest.write_bytes(rom(2))
            report=sync.plan(source,root);sync.transfer(report,True,False)
            self.assertEqual(dest.read_bytes(),rom(2))
            self.assertNotEqual(report['games'][0]['destination'],str(dest))
            self.assertEqual(Path(report['games'][0]['destination']).read_bytes(),rom(1))

    def test_changed_source_rolls_back_only_new_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            source,root=self.setup_dirs(folder);path=source/'Own.z64';path.write_bytes(rom())
            report=sync.plan(source,root);path.write_bytes(rom(3))
            with self.assertRaisesRegex(OSError,'Origem mudou'):sync.transfer(report,True,False)
            self.assertFalse(Path(report['games'][0]['destination']).exists())
            self.assertEqual(path.read_bytes(),rom(3))
            self.assertFalse((root/'roms/n64/.tab5-n64-transfer.lock').exists())

    def test_index_failure_restores_exact_catalog_and_index(self):
        with tempfile.TemporaryDirectory() as folder:
            source,root=self.setup_dirs(folder);(source/'Own.z64').write_bytes(rom())
            catalog=root/'library/games.json';catalog.parent.mkdir();catalog.write_bytes(b'{"schema_version":1,"games":[]}\n')
            index=root/'cache/library-v1.t5i';index.parent.mkdir();index.write_bytes(b'original index')
            report=sync.plan(source,root)
            with patch.object(sync.library_index,'build',side_effect=OSError('SD full')):
                with self.assertRaises(OSError):sync.transfer(report,True)
            self.assertEqual(catalog.read_bytes(),b'{"schema_version":1,"games":[]}\n')
            self.assertEqual(index.read_bytes(),b'original index')
            self.assertTrue(list(index.parent.glob('*.bak')))
            self.assertFalse(report['complete'])

    def test_invalid_rom_and_symlink_target_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            source,root=self.setup_dirs(folder);path=source/'Own.z64';path.write_bytes(b'bad')
            with self.assertRaises(ValueError):sync.plan(source,root)
            path.write_bytes(rom());(root/'roms').mkdir();outside=Path(folder)/'outside';outside.mkdir()
            (root/'roms/n64').symlink_to(outside,target_is_directory=True)
            with self.assertRaises(ValueError):sync.plan(source,root)


if __name__=='__main__':unittest.main()
