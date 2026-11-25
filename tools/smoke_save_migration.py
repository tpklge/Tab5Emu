#!/usr/bin/env python3
"""Exercise migrated GB/GBC states and raw Gwenesis conversion on the Tab5."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from build_8bit_test_roms import gameboy
from build_md_test_rom import build as megadrive
from emulator_usb import prepare
import migrate_saves as migration
from fixture_catalog import temporary_fixture_catalog


def fixture(system):
    rom=bytearray(megadrive()[0] if system=='genesis' else gameboy(system=='gbc'))
    if system=='genesis':
        rom[0x120:0x128]=b'LEGACY T'
    else:
        rom[0x134:0x143]=b'TAB5 LEGACY'.ljust(15,b'\0')
        checksum=0
        for value in rom[0x134:0x14d]:checksum=(checksum-value-1)&255
        rom[0x14d]=checksum;rom[0x14e:0x150]=bytes(2);rom[0x14e:0x150]=(sum(rom)&65535).to_bytes(2,'big')
    assert len(rom)==32768
    rom=bytes(rom);digest=hashlib.sha256(rom).hexdigest()
    ext='.md' if system=='genesis' else '.'+system
    name=f'tab5-legacy-{system}-{digest[:12]}{ext}'
    return rom,name


def run(c, report, work):
    work.mkdir(parents=True)
    settings = c.settings()['settings']
    report['systems'] = {}
    try:
        c.settings('resume=0')
        for system in ('gb', 'gbc', 'genesis'):
            rom,name=fixture(system);digest=hashlib.sha256(rom).hexdigest()
            folder = 'megadrive' if system == 'genesis' else system
            remote = f'Tab5-Emu/roms/{folder}/{name}'
            battery = f'Tab5-Emu/saves/{system}/{digest}/battery.sav'
            state = f'Tab5-Emu/states/{system}/{digest}/core-v1/{Path(name).stem}'
            if c.stat(remote) is None: c.write_file(remote, rom)
            assert c.read_file(remote) == rom
            root = work/system; root.mkdir()
            rom_path = root/name; rom_path.write_bytes(rom)
            source = root/'legacy-battery.sav'
            seed = (b'T5\x11' if system == 'genesis' else b'T\x11')
            source.write_bytes(seed+bytes([255])*(8192-len(seed)))
            content = root/'Tab5-Emu'; content.mkdir()
            common = ['--root', str(content), '--system', system, '--rom', str(rom_path)]
            if c.stat(battery) is None:
                args = migration.parser().parse_args(common+['--kind', 'battery', '--source', str(source), '--apply'])
                plan = migration.migrate(args); dest = Path(plan['destination'])
                assert c.stat(battery+'.sha256') is None and c.stat(battery+'.bak') is None
                c.write_file(battery+'.sha256', Path(str(dest)+'.sha256').read_bytes()); c.write_file(battery, dest.read_bytes())
            baseline = c.read_file(battery)[2 if system == 'genesis' else 1]
            template_slot = next(n for n in range(5, -1, -1) if c.stat(state+f'_{n}_{system}.sav') is None and c.stat(state+f'_{n}_{system}.sav.bak') is None)
            import_slot = next(n for n in range(5, -1, -1) if n != template_slot and c.stat(state+f'_{n}_{system}.sav') is None and c.stat(state+f'_{n}_{system}.sav.bak') is None)
            c.launch(remote); time.sleep(.4); c.save(template_slot)
            template = c.read_file(state+f'_{template_slot}_{system}.sav'); c.quit()
            current_battery = c.read_file(battery)
            template_path = root/'current-template.sav'; template_path.write_bytes(template)
            old_state = root/'legacy-basename.sav'
            if system == 'genesis':
                core_size = int.from_bytes(template[56:60], 'little')
                old_state.write_bytes(template[112:112+core_size])
            else: old_state.write_bytes(template)
            old_battery = root/'legacy-state-battery.sav'; old_battery.write_bytes(current_battery)
            args = migration.parser().parse_args(common+['--kind', 'state', '--source', str(old_state), '--slot', str(import_slot), '--template', str(template_path), '--battery', str(old_battery), '--apply'])
            plan = migration.migrate(args)
            target = 'Tab5-Emu/'+Path(plan['destination']).relative_to(content.resolve()).as_posix()
            assert c.stat(target) is None and c.stat(target+'.bak') is None
            migrated = Path(plan['destination']).read_bytes(); c.write_file(target, migrated)
            # A cold boot increments the battery again. Loading must restore
            # the previous timeline, including the original raw state's RAM.
            c.launch(remote); time.sleep(.35); c.load(import_slot); c.quit()
            assert c.read_file(battery) == current_battery
            assert old_state.read_bytes() == (template[112:112+core_size] if system == 'genesis' else template)
            assert source.read_bytes().startswith(seed)
            report['systems'][system] = {'complete': True, 'rom_sha256': digest, 'battery_before': baseline,
                                        'counter_after_imported_state': current_battery[2 if system == 'genesis' else 1],
                                        'battery_imported': True, 'legacy_state_source_bytes': old_state.stat().st_size,
                                        'migrated_state_bytes': len(migrated), 'state_load_restores_battery': True,
                                        'source_preserved': True, 'raw_md_converted': system == 'genesis'}
        report['complete'] = True
    finally:
        if c.status()['state'] == 'game': c.quit()
        c.settings('resume='+str(settings['resume']))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--elf-sha256', required=True); p.add_argument('--report', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    args = p.parse_args()
    if args.report.exists() or args.work.exists(): raise FileExistsError('Report/work already exists')
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report = {'complete': False, 'elf_sha256': args.elf_sha256}
    try:
        with Link('tab5-emu').open(timeout_s=30) as link:
            c = Control(link); assert c.info()['elf_sha256'] == args.elf_sha256 and c.status()['state'] == 'menu'
            fixtures=[]
            for system in ('gb','gbc','genesis'):
                data,name=fixture(system);folder='megadrive' if system=='genesis' else system
                fixtures.append((f'Tab5-Emu/roms/{folder}/{name}',data))
            with temporary_fixture_catalog(c,fixtures,args.work):
                run(c, report, args.work)
            report['catalog_restored']=True
    except Exception as error:
        report['complete'] = False; report['error'] = repr(error); raise
    finally:
        args.report.write_text(json.dumps(report, indent=2)+'\n'); print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
