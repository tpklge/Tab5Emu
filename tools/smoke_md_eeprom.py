#!/usr/bin/env python3
"""Physical EEPROM + legacy battery import; own 0BSD fixtures only."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from build_md_eeprom_rom import build
from emulator_usb import prepare
import migrate_saves
from fixture_catalog import temporary_fixture_catalog


def run(c, report, work):
    from emulink.control import ControlError
    settings = c.settings()['settings']
    report['boards'] = {}
    work.mkdir(parents=True, exist_ok=True)
    try:
        c.settings('resume=0'); c.settings('input_test=1')
        for index, board in enumerate(('sega', 'ea', 'acclaim16'), 1):
            rom = build(board); digest = hashlib.sha256(rom).hexdigest()
            name = f'tab5-md-eeprom-{board}-{digest[:12]}.md'
            remote = 'Tab5-Emu/roms/megadrive/'+name
            if c.stat(remote) is None: c.write_file(remote, rom)
            assert c.read_file(remote) == rom
            battery = f'Tab5-Emu/saves/genesis/{digest}/battery.sav'
            state = f'Tab5-Emu/states/genesis/{digest}/core-v1/{Path(name).stem}'
            result = {'rom_sha256': digest, 'complete': False}
            report['boards'][board] = result
            if c.stat(battery) is None:
                seed = bytes((7, ord('T'), ord('5'), index))+bytes([255])*((256 if board == 'acclaim16' else 128)-4)
                work.mkdir(parents=True, exist_ok=True)
                legacy = work/(board+'-legacy.sav'); legacy.write_bytes(seed)
                local_rom = work/name; local_rom.write_bytes(rom)
                root = work/(board+'-sd'); root.mkdir()
                args = migrate_saves.parser().parse_args(['--root', str(root), '--system', 'genesis', '--kind', 'battery', '--rom', str(local_rom), '--source', str(legacy), '--apply'])
                plan = migrate_saves.migrate(args)
                dest = Path(plan['destination'])
                outputs = [(battery+'.sha256', Path(str(dest)+'.sha256').read_bytes()), (battery, dest.read_bytes())]
                assert all(c.stat(path) is None and c.stat(path+'.bak') is None for path, _ in outputs)
                for path, payload in outputs: c.write_file(path, payload)
                assert legacy.read_bytes() == seed
                result['legacy_import'] = {'source_sha256': hashlib.sha256(seed).hexdigest(), 'original_preserved': True, 'journal_sha256': hashlib.sha256(outputs[0][1]).hexdigest()}
            baseline = c.read_file(battery)[0]
            slot = next(n for n in range(5, -1, -1) if c.stat(state+f'_{n}_genesis.sav') is None and c.stat(state+f'_{n}_genesis.sav.bak') is None)
            c.launch(remote); time.sleep(.5); assert c.status()['game_frames'] > 0
            c.save(slot)
            saved = c.read_file(state+f'_{slot}_genesis.sav')
            assert saved[:8] == b'T5MDST01' and int.from_bytes(saved[8:12], 'little') == 4 and int.from_bytes(saved[12:16], 'little') == 144
            assert saved[112:116] == b'EE\1'+bytes((index,))
            assert saved[-(256 if board == 'acclaim16' else 128):][:4] == bytes(((baseline+1)&255, ord('T'), ord('5'), index))
            c.press(['start'], hold_ms=180); c.save(slot)
            updated = c.read_file(state+f'_{slot}_genesis.sav')
            assert updated[-(256 if board == 'acclaim16' else 128)] == (baseline+2)&255
            # Use the .bak generation through the same runtime validator.
            assert c.read_file(state+f'_{slot}_genesis.sav.bak') == saved
            restore_slot = next(n for n in range(5, -1, -1) if n != slot and c.stat(state+f'_{n}_genesis.sav') is None and c.stat(state+f'_{n}_genesis.sav.bak') is None)
            restore = state+f'_{restore_slot}_genesis.sav'
            c.write_file(restore, saved)
            c.load(restore_slot); c.quit()
            first = c.read_file(battery)
            assert first[:4] == bytes(((baseline+1)&255, ord('T'), ord('5'), index))
            c.launch(remote); time.sleep(.4); c.quit()
            second = c.read_file(battery)
            assert second[0] == (baseline+2)&255 and c.read_file(battery+'.bak') == first
            c.launch(remote); time.sleep(.3); c.load(restore_slot)
            invalid = []
            for label, offset in (('digest', -1), ('board', 115), ('protocol_counter', 121)):
                broken = bytearray(saved)
                if label=='digest': broken[offset] ^= 128
                else: broken[offset] = 255
                if label != 'digest':
                    broken[60:92] = bytes(32); broken[60:92] = hashlib.sha256(broken).digest()
                c.write_file(restore, bytes(broken))
                try: c.load(restore_slot)
                except ControlError: invalid.append(label)
                else: raise AssertionError('Invalid EEPROM state accepted: '+label)
            c.rm(restore); c.quit()
            assert c.read_file(battery) == first
            result.update(complete=True, counter_before=baseline, first_counter=first[0], second_counter=second[0],
                          bytes=len(first), state_version=4, state_bytes=len(saved), state_restores_eeprom=True,
                          cold_boot_reads_eeprom=True, start_guest_write=True, previous_battery_retained=True,
                          invalid_states_rejected=invalid)
        report['complete'] = True
    finally:
        if c.status()['state'] == 'game': c.quit()
        c.settings('resume='+str(settings['resume'])); c.settings('input_test='+str(settings['input_test']))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-sha256', required=True)
    p.add_argument('--report', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True, help='New local directory; keeps original legacy fixtures')
    args = p.parse_args()
    if args.report.exists() or args.work.exists(): raise FileExistsError('Report/work already exists')
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report = {'complete': False, 'elf_sha256': args.elf_sha256}
    try:
        with Link('tab5-emu').open(timeout_s=30) as link:
            c = Control(link); assert c.info()['elf_sha256'] == args.elf_sha256
            assert c.status()['state'] == 'menu'
            fixtures=[]
            for board in ('sega', 'ea', 'acclaim16'):
                data=build(board); digest=hashlib.sha256(data).hexdigest()
                fixtures.append((f'Tab5-Emu/roms/megadrive/tab5-md-eeprom-{board}-{digest[:12]}.md',data))
            with temporary_fixture_catalog(c,fixtures,args.work):
                run(c, report, args.work)
            report['catalog_restored']=True
    except Exception as error:
        report['complete'] = False; report['error'] = repr(error); raise
    finally:
        args.report.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
