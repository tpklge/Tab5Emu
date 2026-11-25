#!/usr/bin/env python3
"""Equal-size save corruption checks restricted to original 0BSD fixtures."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from build_8bit_test_roms import gameboy, snes
from build_md_test_rom import build as megadrive
from emulator_usb import prepare

GBA = 'ab627e010bda9132328e5b2f69b5ec1c98f19a5a45737a9bce1dc4553d03d234'


def test(c, system, folder, name, digest, size, archive_previous=False):
    rom = 'Tab5-Emu/roms/' + folder + '/' + name
    assert hashlib.sha256(c.read_file(rom)).hexdigest() == digest
    path = 'Tab5-Emu/saves/' + system + '/' + digest + '/battery.sav'
    # By default only the old truncation sentinel is eligible. The explicit
    # archive option also accepts a same-size prior reject of this exact
    # generated fixture; copy/readback preserves it before removal.
    if c.stat(path+'.rejected'):
        old = c.read_file(path+'.rejected')
        if old != b'X':
            assert archive_previous and len(old)==size and old[0]==0x54, 'unknown reject preserved'
        archive = path+'.rejected.before-integrity-'+hashlib.sha256(old).hexdigest()[:16]
        if not c.stat(archive): c.write_file(archive, old)
        assert c.read_file(archive) == old; c.rm(path+'.rejected')

    def boot():
        c.launch(name); time.sleep(.5); c.settings('input_test=1')
        status = c.status(); assert status['state'] == 'game' and status['game_frames'] > 0, status
        if system == 'gba': c.press(['start'])
        c.quit(); time.sleep(.3)
        data = c.read_file(path); assert len(data) == size
        return data

    first = boot(); second = boot(); assert first != second
    previous = c.read_file(path+'.bak'); assert previous == first
    journal = c.read_file(path+'.sha256').decode('ascii')
    assert hashlib.sha256(first).hexdigest() in journal and hashlib.sha256(second).hexdigest() in journal
    corrupt = bytearray(second); corrupt[-1] ^= 128
    c.write_file(path, bytes(corrupt))
    recovered = boot()
    assert c.read_file(path+'.rejected') == bytes(corrupt)
    assert c.read_file(path+'.bak') == previous
    # Each guest increments the prior save once; recovering from first must
    # reproduce second, not increment the corrupted primary a second time.
    assert recovered == second
    return {'system': system, 'rom_sha256': digest, 'raw_bytes': size,
            'journal_attests_both_generations': True,
            'equal_size_corruption_detected': True,
            'backup_recovered_by_guest': True, 'rejected_bytes_preserved': True}


def main():
    p = argparse.ArgumentParser(description=__doc__); p.add_argument('--report', type=Path, required=True)
    p.add_argument('--archive-previous-fixture-reject',action='store_true',help='Archive/readback a prior reject of the exact generated fixture before retesting')
    args = p.parse_args()
    if args.report.exists(): raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report = {'complete': False, 'systems': []}
    try:
        with Link('tab5-emu').open(timeout_s=15) as link:
            c = Control(link); assert c.status()['state'] == 'menu'; report['info'] = c.info()
            old = c.settings()['settings']
            fixtures = []
            for system, builder, ext in [('gb', gameboy, '.gb'), ('gbc', lambda:gameboy(True), '.gbc'), ('snes', snes, '.sfc')]:
                digest = hashlib.sha256(builder()).hexdigest()
                fixtures.append((system, system, 'tab5-sram-'+digest[:12]+ext, digest, 8192))
            fixtures.append(('gba', 'gba', 'tab5-diagnostic-ab627e01.gba', GBA, 131072))
            digest = hashlib.sha256(megadrive()[0]).hexdigest()
            fixtures.append(('genesis', 'megadrive', 'tab5-md-sram-'+digest[:12]+'.md', digest, 8192))
            try:
                c.settings('resume=0')
                for fixture in fixtures:
                    result = test(c, *fixture,archive_previous=args.archive_previous_fixture_reject); report['systems'].append(result); print(result, flush=True)
                report['complete'] = True
            finally:
                c.settings('resume='+str(old['resume'])); c.settings('input_test='+str(old['input_test']))
    except Exception as error:
        report['error'] = repr(error); raise
    finally:
        args.report.write_text(json.dumps(report, indent=2)+'\n')


if __name__ == '__main__': main()
