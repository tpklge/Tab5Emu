#!/usr/bin/env python3
"""Physical smoke test of our 0BSD GBA fixture; writes only its own saves."""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path
from emulator_usb import prepare

# Identity of the fixture, not a user's ROM or a name-based save namespace.
SHA256 = 'ab627e010bda9132328e5b2f69b5ec1c98f19a5a45737a9bce1dc4553d03d234'


def inspect_frame(control):
    digits = {v: i for i, v in enumerate([0x3f, 6, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 7, 0x7f, 0x6f])}
    points = [(7, 1), (13, 6), (13, 17), (7, 23), (1, 17), (1, 6), (7, 12)]
    # The simple fixture draws VRAM directly. Retry a partially drawn digit;
    # use the units digit only, then verify the complete SRAM byte on disk.
    for _ in range(12):
        frame = control.frame()
        if (frame.width, frame.height, frame.format) != (240, 160, 'rgb565le'):
            raise ValueError('unexpected test framebuffer')
        pixels = struct.unpack('<38400H', frame.data)
        bits = sum(1 << i for i, (dx, dy) in enumerate(points) if pixels[(34 + dy) * 240 + 201 + dx])
        marker = [(x, y, pixels[y * 240 + x]) for y in range(72, 160) for x in range(240) if pixels[y * 240 + x]]
        if bits in digits and len(marker) == 25:
            return {'counter_units': digits[bits],
                    'marker': [min(p[0] for p in marker), min(p[1] for p in marker)],
                    'marker_color': marker[0][2]}
        time.sleep(.05)
    raise AssertionError('fixture framebuffer did not stabilize')


def smoke(control, report):
    from emulink.control import Buttons
    rom = 'Tab5-Emu/roms/gba/tab5-diagnostic-ab627e01.gba'
    if hashlib.sha256(control.read_file(rom)).hexdigest() != SHA256:
        raise ValueError('fixture ROM hash differs; no save modified')
    status = control.status()
    if status.get('state') != 'game' or status.get('rom') != '/sdcard/' + rom:
        raise ValueError('launch the exact fixture first')
    report['firmware'] = status['firmware']
    save = f'Tab5-Emu/saves/gba/{SHA256}/battery.sav'
    previous_save = control.read_file(save) if control.stat(save) else None
    report['initial'] = inspect_frame(control)
    control.press(['right', 'up'], hold_ms=200)
    report['diagonal'] = inspect_frame(control)
    a, b = report['initial']['marker'], report['diagonal']['marker']
    if a[0] >= 210 or a[1] <= 90:
        raise ValueError('marker too close to edge; relaunch fixture before testing')
    assert b[0] > a[0] and b[1] < a[1]
    try:
        control.buttons(Buttons.A)
        time.sleep(.1)
        report['button_a'] = inspect_frame(control)
        assert report['button_a']['marker_color'] != report['diagonal']['marker_color']
    finally:
        control.buttons(0)
    control.press(['start'])
    report['incremented'] = inspect_frame(control)
    assert report['incremented']['counter_units'] == (report['initial']['counter_units'] + 1) % 10
    control.save(5)
    control.press(['start'])
    report['second_increment'] = inspect_frame(control)
    assert report['second_increment']['counter_units'] == (report['incremented']['counter_units'] + 1) % 10
    control.load(5)
    time.sleep(.15)
    report['state_restored'] = inspect_frame(control)
    assert report['state_restored']['counter_units'] == report['incremented']['counter_units']
    resume = control.settings()['settings']['resume']
    try:
        control.settings('resume=0')
        control.quit()
        data = control.read_file(save)
        report['battery'] = {'size': len(data), 'first_two_bytes': data[:2].hex(),
                             'sha256': hashlib.sha256(data).hexdigest()}
        assert len(data) == 131072 and data[0] == 0x54
        # gpSP serializes CPU/PPU/backup protocol state, but deliberately
        # excludes gamepak_backup SRAM bytes. Loading a state preserves the
        # latest cartridge save rather than rolling it back with CPU RAM.
        assert data[1] % 10 == report['second_increment']['counter_units']
        report['savestate_preserves_latest_sram'] = True
        control.launch('tab5-diagnostic-ab627e01.gba')
        time.sleep(.2)
        report['fresh_boot_battery'] = inspect_frame(control)
        assert report['fresh_boot_battery']['counter_units'] == data[1] % 10
        control.quit()
        backup = control.read_file(save + '.bak')
        assert len(backup)==131072 and backup[0]==0x54
        if previous_save is not None and previous_save!=data:
            assert backup==previous_save
            report['previous_distinct_generation_preserved']=True
        report['backup_valid'] = True
    finally:
        control.settings('resume=' + str(resume))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report = {'rom_sha256': SHA256, 'scripted_usb_input': True, 'complete': False}
    try:
        with Link('tab5-emu').open(timeout_s=10) as link:
            smoke(Control(link), report)
        report['complete'] = True
    except Exception as error:
        report['error'] = repr(error)
        raise
    finally:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        with args.report.open('x') as output:
            json.dump(report, output, indent=2)
            output.write('\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
