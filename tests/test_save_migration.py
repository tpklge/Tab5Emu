import argparse
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location('migration', Path(__file__).resolve().parents[1]/'tools/migrate_saves.py')
m = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(m)


def gb_rom(mapper=6, ram=0, color=False):
    rom = bytearray(32768)
    rom[0x143] = 0x80 if color else 0
    rom[0x147:0x14a] = bytes((mapper, 0, ram))
    return bytes(rom)


def gb_state(rom):
    color = bool(rom[0x143]&128)
    iram, vram = (8, 4) if color else (2, 2)
    data = bytearray((1+iram+vram+m.gb_geometry(rom)*2)*4096)
    tags = ((b'GbSs', 0x105), (b'cgb ', int(color)), (b'iram', 1), (b'vram', 1+iram),
            (b'sram', 1+iram+vram), (b'hi  ', 3328), (b'pal ', 3584), (b'oam ', 3840), (b'wav ', 3312))
    for i, (key, value) in enumerate(tags):
        struct.pack_into('<4sI', data, 8*i, key, value)
    return bytes(data)


def md_rom():
    rom = bytearray(32768)
    struct.pack_into('>II', rom, 0, 0xfffffc, 512)
    rom[0x1b0:0x1b4] = b'RA\xf8\x20'
    struct.pack_into('>II', rom, 0x1b4, 0x200001, 0x203fff)
    return bytes(rom)


def md_template(rom, ram_size=8192, version=2):
    core = b'cpu'.ljust(28, b'\0')+struct.pack('<I', 4)+b'\1\2\3\4'
    header=144 if version==4 else 112
    data = bytearray(header+len(core)+ram_size)
    data[:8] = b'T5MDST01'
    struct.pack_into('<II', data, 8, version, header)
    data[16:48] = bytes.fromhex(m.sha(rom))
    struct.pack_into('<III', data, 48, ram_size, 1, len(core))
    data[92:96] = b'PAD6'
    for off in (96, 100, 104):
        data[off:off+4] = bytes((0, 0, 15, 1))
    if version==4:data[112:120]=b'EE\1\1\1\1\0\0'
    data[header:header+len(core)] = core
    data[60:92] = bytes.fromhex(m.sha(data))
    return bytes(data), core


def gba_state():
    data = bytearray(struct.pack('<I', 416*1024))
    for key, value in ((b'info-magic', 0x6bac0de), (b'info-version', 0x10004)):
        data += b'\x10'+key+b'\0'+struct.pack('<I', value)
    # A nested document exercises bounds and recursion in the actual format.
    nested = struct.pack('<I', 12)+b'\x10x\0'+struct.pack('<I', 7)+b'\0'
    data += b'\x03cpu\0'+nested
    padding = 416*1024-len(data)-16
    data += b'\x05zpadding\0'+struct.pack('<I', padding)+bytes(padding+2)
    assert len(data) == 416*1024
    return bytes(data)


class MigrationTests(unittest.TestCase):
    def test_physical_fixtures_have_valid_geometry(self):
        sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
        try:
            from smoke_save_migration import fixture
            from build_md_eeprom_rom import build
            for system in ('gb','gbc','genesis'):
                rom,_=fixture(system)
                self.assertEqual(len(rom),32768)
                self.assertEqual(m.md_geometry(rom) if system=='genesis' else m.gb_geometry(rom)*8192,8192)
            for board,size in (('sega',128),('ea',128),('acclaim16',256)):
                self.assertEqual(m.md_geometry(build(board)),size)
        finally:
            sys.path.pop(0)

    def test_native_rtc_formats_and_invalid_day(self):
        rom = gb_rom(16, 3)
        for version in (1, 2):
            data = struct.pack('<8I', 0x31544352 if version == 1 else 0x32544352, 512 if version == 1 else 256, 23, 59, 58, 59, 0, 1)
            if version == 2: data += struct.pack('<Q', 1795500000)
            self.assertEqual(m.rtc_data('gb', rom, data), f'rtc-v{version}.bin')
            bad = bytearray(data); struct.pack_into('<I', bad, 4, 1024)
            with self.assertRaises(ValueError): m.rtc_data('gb', rom, bad)
        with self.assertRaises(ValueError): m.rtc_data('gb', gb_rom(), bytes(32))

    def test_rtc_import_refuses_a_different_existing_clock_version(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);rom=root/'clock.gb';rom.write_bytes(gb_rom(16,3))
            source=root/'clock-old.bin';source.write_bytes(struct.pack('<8I',0x31544352,0,0,0,0,0,0,0))
            args=m.parser().parse_args(['--root',str(root),'--system','gb','--kind','rtc','--rom',str(rom),'--source',str(source)])
            destination,_,_=m.prepare(args);destination.parent.mkdir(parents=True)
            newer=destination.parent/'rtc-v2.bin';newer.write_bytes(b'preserve newer clock')
            with self.assertRaises(FileExistsError):m.prepare(args)
            self.assertEqual(newer.read_bytes(),b'preserve newer clock')

    def test_mbc2_plan_apply_no_clobber_and_journal(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            rom = root/'game.gb'; rom.write_bytes(gb_rom())
            old = root/'old.sav'; original = bytes(range(256))*2; old.write_bytes(original)
            args = m.parser().parse_args(['--root', str(root), '--system', 'gb', '--kind', 'battery', '--rom', str(rom), '--source', str(old)])
            plan = m.migrate(args)
            self.assertFalse(plan['applied'])
            dest = Path(plan['destination']); self.assertFalse(dest.exists())
            args.apply = True; result = m.migrate(args)
            self.assertTrue(result['applied']); self.assertEqual(old.read_bytes(), original)
            data = dest.read_bytes(); self.assertEqual(len(data), 8192)
            self.assertEqual(data[:512], bytes(x&15 for x in original))
            self.assertEqual(Path(str(dest)+'.sha256').read_text(), f'T5B1 8192\n{m.sha(data)}\n')
            self.assertEqual(json.loads(Path(str(dest)+'.migration.json').read_text())['source_sha256'], m.sha(original))
            with self.assertRaises(FileExistsError): m.migrate(args)

    def test_failed_write_rolls_back_only_own_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); rom = root/'game.gb'; rom.write_bytes(gb_rom())
            old = root/'old.sav'; old.write_bytes(bytes(512))
            args = m.parser().parse_args(['--root', str(root), '--system', 'gb', '--kind', 'battery', '--rom', str(rom), '--source', str(old), '--apply'])
            dest, _, _ = m.prepare(args)
            with patch.object(m.os, 'fsync', side_effect=[None, None, OSError('disk full')]):
                with self.assertRaises(OSError): m.migrate(args)
            self.assertEqual(list(dest.parent.iterdir()), [])
            self.assertEqual(old.read_bytes(), bytes(512))

    def test_reserved_backup_and_symlink_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); rom = root/'game.gb'; rom.write_bytes(gb_rom())
            old = root/'old.sav'; old.write_bytes(bytes(512))
            args = m.parser().parse_args(['--root', str(root), '--system', 'gb', '--kind', 'battery', '--rom', str(rom), '--source', str(old)])
            dest, _, _ = m.prepare(args); dest.parent.mkdir(parents=True)
            backup = Path(str(dest)+'.bak'); backup.write_bytes(b'keep')
            with self.assertRaises(FileExistsError): m.prepare(args)
            self.assertEqual(backup.read_bytes(), b'keep')
            backup.unlink(); (root/'states').symlink_to(dest.parent, target_is_directory=True)
            args.kind = 'state'; args.slot = 0; old.write_bytes(gb_state(rom.read_bytes()))
            with self.assertRaisesRegex(ValueError, 'link simbolico'): m.prepare(args)

    def test_legacy_source_invalid_journal_is_not_ignored(self):
        with tempfile.TemporaryDirectory() as folder:
            source = Path(folder)/'legacy.sav'; source.write_bytes(bytes(512))
            sidecar = Path(str(source)+'.sha256'); sidecar.write_text('T5B1 512\n'+'0'*64+'\n')
            with self.assertRaises(ValueError): m.verify_source_journal(source, source.read_bytes())
            backup = Path(str(source)+'.sha256.bak')
            backup.write_text(f'T5B1 512\n{m.sha(source.read_bytes())}\n')
            m.verify_source_journal(source, source.read_bytes())
            generation = Path(str(source)+'.bak'); generation.write_bytes(source.read_bytes())
            m.verify_source_journal(generation, generation.read_bytes())

    def test_fixture_catalog_rolls_back_after_test_failure(self):
        spec = importlib.util.spec_from_file_location('fixture_catalog', Path(__file__).resolve().parents[1]/'tools/fixture_catalog.py')
        catalog = importlib.util.module_from_spec(spec); spec.loader.exec_module(catalog)
        class Control:
            def __init__(self):
                self.files = {'Tab5-Emu/library/games.json': b'original catalog', 'Tab5-Emu/cache/library-v1.t5i': b'original index'}
                self.page = 7
            def stat(self, p): return {'size': len(self.files[p])} if p in self.files else None
            def read_file(self, p): return self.files[p]
            def write_file(self, p, data): self.files[p] = data
            def rm(self, p): del self.files[p]
            def status(self): return {'state': 'menu'}
            def reload_roms(self): pass
            def settings(self, change=None):
                if change: self.page = int(change.split('=')[1])
                return {'library': {'page': self.page, 'error': ''}, 'library_loading': False}
        c = Control(); before = dict(c.files)
        with tempfile.TemporaryDirectory() as folder, patch.object(catalog.time, 'sleep'):
            with self.assertRaisesRegex(RuntimeError, 'guest test failed'):
                with catalog.temporary_fixture_catalog(c, [('Tab5-Emu/roms/gb/own.gb', b'owned')], Path(folder)/'proof'):
                    self.assertNotIn('Tab5-Emu/cache/library-v1.t5i', c.files)
                    self.assertNotEqual(c.files['Tab5-Emu/library/games.json'], before['Tab5-Emu/library/games.json'])
                    raise RuntimeError('guest test failed')
            self.assertEqual(c.page, 7)
            self.assertTrue(all(c.files[p] == data for p, data in before.items()))

    def test_gb_state_geometry_and_corrupt_offsets(self):
        for color in (False, True):
            rom = gb_rom(0x13, 3, color); state = gb_state(rom)
            self.assertEqual(m.state_data('gbc' if color else 'gb', rom, state, None), state)
            bad = bytearray(state); struct.pack_into('<I', bad, 20, 999)
            with self.assertRaises(ValueError): m.gb_state(rom, bad)
            with self.assertRaises(ValueError): m.gb_state(rom, state[:-1])

    def test_gba_backup_requires_explicit_matching_type(self):
        rom = bytes(192)+b'EEPROM_V124'
        with self.assertRaises(ValueError): m.battery_data('gba', rom, bytes(512))
        converted = m.battery_data('gba', rom, b'X'*512, 'eeprom512')
        self.assertEqual(converted, b'X'*512+bytes([255])*(131072-512))
        with self.assertRaises(ValueError): m.battery_data('gba', rom, bytes(65536), 'flash64k')
        with self.assertRaises(ValueError): m.battery_data('gba', rom, bytes(8192), 'eeprom512')

    def test_gba_bson_same_schema_and_malformed_lengths(self):
        state = gba_state()
        self.assertEqual(m.state_data('gba', bytes(192), state, state), state)
        with self.assertRaises(ValueError): m.state_data('gba', bytes(192), state, None)
        for offset in (0, 27, 60):
            bad = bytearray(state); bad[offset:offset+4] = bytes([255])*4
            with self.assertRaises(ValueError): m.state_data('gba', bytes(192), bad, state)
        bad = bytearray(state); bad[16] ^= 1
        with self.assertRaises(ValueError): m.state_data('gba', bytes(192), bad, state)

    def test_md_raw_conversion_uses_legacy_battery_not_template(self):
        rom = md_rom(); template, core = md_template(rom); battery = b'S'*8192
        converted = m.state_data('genesis', rom, core, template, battery)
        self.assertEqual(converted[112:112+len(core)], core)
        self.assertEqual(converted[-8192:], battery)
        m.md_state(converted, m.sha(rom), 8192)
        self.assertEqual(m.state_data('genesis', rom, converted, template), converted)
        old=bytearray(converted);struct.pack_into('<I',old,8,1);old[92:112]=bytes(20)
        old[60:92]=bytes(32);old[60:92]=bytes.fromhex(m.sha(old))
        self.assertEqual(m.state_data('genesis',rom,bytes(old),template),old)
        with self.assertRaises(ValueError): m.state_data('genesis', rom, core, template)
        bad = bytearray(core); bad[0] ^= 1
        with self.assertRaises(ValueError): m.state_data('genesis', rom, bad, template, battery)
        bad = bytearray(converted); bad[-1] ^= 1
        with self.assertRaises(ValueError): m.state_data('genesis', rom, bad, template)
        with self.assertRaises(ValueError): m.md_state(converted, '0'*64, 8192)

    def test_md_eeprom_state_import_rejects_other_board_and_invalid_wire_phase(self):
        rom=bytearray(md_rom());rom[0x180:0x18e]=b'GM MK-1215 -00'.ljust(14);rom[0x1b2]=0xe8
        state,core=md_template(rom,128,4)
        self.assertEqual(m.state_data('genesis',rom,state,state),state)
        with self.assertRaises(ValueError):m.state_data('genesis',rom,core,state,bytes(128))
        for off,value in ((115,2),(119,4),(127,255)):
            bad=bytearray(state);bad[off]=value
            if off==119:bad[121]=8;bad[116]=0
            bad[60:92]=bytes(32);bad[60:92]=bytes.fromhex(m.sha(bad))
            with self.assertRaises(ValueError):m.state_data('genesis',rom,bad,state)

    def test_snes_sram_layout_explicit_and_raw_state_refused(self):
        rom = bytearray(32768); rom[0x7fd5] = 0x20; rom[0x7fd8] = 3
        self.assertEqual(m.battery_data('snes', rom, bytes(8192), snes_layout='lorom'), bytes(8192))
        with self.assertRaises(ValueError): m.battery_data('snes', rom, bytes(8192))
        with self.assertRaises(ValueError): m.battery_data('snes', rom, bytes(8192), snes_layout='hirom')
        with self.assertRaises(ValueError): m.state_data('snes', rom, bytes(8192), bytes(8192))

    def test_md_serial_identity_matches_runtime_and_sram_patches(self):
        rom = bytearray(md_rom()); rom[0x180:0x18e] = b'GM T-50176-00 '.ljust(14)
        # Keep the header 14 bytes; a linear SRAM hack must win.
        self.assertEqual(m.md_geometry(rom), 8192)
        rom[0x1b2] = 0xe8
        self.assertEqual(m.md_geometry(rom), 128)
        rom[0x180:0x18e] = b'GM T-081326-00'.ljust(14)
        self.assertEqual(m.md_geometry(rom), 256)


if __name__ == '__main__':
    unittest.main()
