#!/usr/bin/env python3
"""Physical persistence checks using only our original 0BSD 68000 ROM."""
import argparse
import hashlib
import json
import struct
import time
from pathlib import Path
from build_md_test_rom import build
from emulator_usb import prepare


def smoke(c, report):
    from emulink.control import ControlError
    rom, _ = build(); digest = hashlib.sha256(rom).hexdigest()
    name = 'tab5-md-sram-' + digest[:12] + '.md'
    remote = 'Tab5-Emu/roms/megadrive/' + name
    assert c.read_file(remote) == rom, 'fixture differs; no saves modified'
    battery = 'Tab5-Emu/saves/genesis/' + digest + '/battery.sav'
    state = 'Tab5-Emu/states/genesis/' + digest + '/core-v1/' + Path(name).stem
    baseline = c.read_file(battery)[2] if c.stat(battery) else 0
    report.update(rom_sha256=digest, baseline=baseline, boots=[])

    def launch():
        c.launch(name); time.sleep(.7); c.settings('input_test=1')
        s = c.status(); assert s['state'] == 'game' and s['game_frames'] > 0, s
        return s

    def image():
        time.sleep(.12); frame = c.frame()
        assert (frame.width, frame.height) == (320, 224)
        assert any(frame.data), 'VDP framebuffer stayed black'
        return hashlib.sha256(frame.data).hexdigest()

    def quit_counter(expected):
        c.quit(); time.sleep(.3); data = c.read_file(battery)
        assert len(data) == 8192 and data[:2] == b'T5' and data[2] == expected, data[:4]
        report['boots'].append({'counter': data[2], 'battery_sha256': hashlib.sha256(data).hexdigest()})
        return data

    launch(); original = image(); c.press(['start'], hold_ms=200)
    incremented = image(); assert incremented != original
    c.save(5); saved = c.read_file(state + '_5_genesis.sav')
    assert saved[:8] == b'T5MDST01'
    c.press(['start'], hold_ms=200); assert image() != incremented
    c.load(5); assert image() == incremented
    first = quit_counter((baseline+2) & 255)
    launch(); second = quit_counter((first[2]+1) & 255)
    assert c.read_file(battery+'.bak') == first
    launch(); c.load(5); assert image() == incremented
    restored = quit_counter(first[2]); assert restored == first
    assert c.read_file(battery+'.bak') == second
    report.update(start_guest_input=True, state_restored_cpu_video_sram=True,
                  cold_init_battery=True, previous_distinct_backup=True,
                  state_bytes=len(saved))
    # Unused slot 4 of this exact generated fixture only. Refuse to overwrite
    # any existing state, even a previous diagnostic run.
    invalid = state + '_4_genesis.sav'
    if c.stat(invalid) is None and c.stat(invalid+'.bak') is None:
        launch(); before = image(); rejected = []
        try:
            broken = bytearray(saved); broken[-1] ^= 128
            wrong_rom = bytearray(saved); wrong_rom[16] ^= 1
            wrong_schema = bytearray(saved); wrong_schema[112] ^= 1
            for data in (wrong_rom, wrong_schema):
                data[60:92] = bytes(32); data[60:92] = hashlib.sha256(data).digest()
            for label, data in [('truncated', b'X'), ('checksum', broken),
                                ('rom_identity', wrong_rom), ('record_schema', wrong_schema)]:
                c.write_file(invalid, bytes(data))
                try: c.load(4)
                except ControlError: rejected.append(label)
                else: raise AssertionError('invalid state accepted: '+label)
                assert image() == before
            report['invalid_states_rejected'] = rejected
        finally:
            c.rm(invalid)
        quit_counter((first[2]+1) & 255)
    else:
        report['invalid_state_test_skipped'] = 'existing diagnostic slot preserved'
    report['complete'] = True


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--report', type=Path, required=True); args = p.parse_args()
    if args.report.exists(): raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report = {'complete': False, 'input_source': 'guest 68000 instructions and scripted Start'}
    try:
        with Link('tab5-emu').open(timeout_s=15) as link:
            c = Control(link); assert c.status()['state'] == 'menu'
            report['info'] = c.info(); old = c.settings()['settings']
            try:
                c.settings('resume=0'); smoke(c, report)
            finally:
                c.settings('resume='+str(old['resume']))
                c.settings('input_test='+str(old['input_test']))
    except Exception as error:
        report['error'] = repr(error); raise
    finally:
        args.report.write_text(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__': main()
