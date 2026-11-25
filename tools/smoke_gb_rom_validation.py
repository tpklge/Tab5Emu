#!/usr/bin/env python3
"""Check GB loader refusals by temporarily replacing only our exact 0BSD ROM."""
import argparse
import hashlib
import json
import time
from pathlib import Path
from build_8bit_test_roms import gameboy
from emulator_usb import prepare


def run(control, expected_elf, report):
    original = gameboy()
    digest = hashlib.sha256(original).hexdigest()
    remote = 'Tab5-Emu/roms/gb/tab5-sram-' + digest[:12] + '.gb'
    if control.info()['elf_sha256'] != expected_elf or control.status()['state'] != 'menu':
        raise ValueError('expected stable ELF and library required; nothing modified')
    if control.read_file(remote) != original:
        raise ValueError('owned fixture differs; nothing modified')
    old = control.settings()['settings']
    report.update(complete=False, elf_sha256=expected_elf, fixture_sha256=digest, refusals=[])
    baseline = control.status()
    control.settings('resume=0')
    try:
        mutations = [('short_header', original[:64], 'cabecalho'),
                     ('missing_banks', original[:512], 'bancos')]
        for label, offset, value, message in [('unsupported_mapper', 0x147, 0x22, 'Mapper'),
                                               ('unsupported_geometry', 0x148, 0x52, 'Tamanho de ROM'),
                                               ('invalid_ram', 0x149, 0xff, 'RAM')]:
            modified = bytearray(original)
            modified[offset] = value
            mutations.append((label, bytes(modified), message))
        for label, payload, expected in mutations:
            control.write_file(remote, payload)
            if control.read_file(remote) != payload:
                raise RuntimeError('fixture writeback differs')
            control.launch(remote, wait=False)
            deadline = time.monotonic() + 15
            while True:
                status = control.status()
                if status['state'] == 'game':
                    raise AssertionError('invalid ROM accepted: ' + label)
                if status.get('launch_error'):
                    if expected not in status['launch_error']:
                        raise AssertionError(status['launch_error'])
                    break
                if time.monotonic() > deadline:
                    raise TimeoutError('no visible ROM error: ' + label)
                time.sleep(.1)
            if status['uptime_ms'] <= baseline['uptime_ms'] or control.info()['elf_sha256'] != expected_elf:
                raise AssertionError('device reset or firmware changed')
            report['refusals'].append({'case': label, 'message': status['launch_error'],
                                       'returned_to_library': True})
            print(label, 'refused and explained', flush=True)
    except Exception as error:
        report['error'] = repr(error)
        raise
    finally:
        # Restore the owned ROM even if a refusal check failed. Saves and
        # states are never changed by the host. Cleanup errors remain visible.
        try:
            if control.status()['state'] == 'game':
                control.quit()
            control.write_file(remote, original)
            if control.read_file(remote) != original:
                raise RuntimeError('original ROM restoration differs')
            report['original_rom_restored'] = True
            control.settings('resume=' + str(old['resume']))
        except Exception as error:
            report['cleanup_error'] = repr(error)
            if 'error' not in report:
                raise
    report['complete'] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf-sha256', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    report = {'complete': False}
    try:
        with Link('tab5-emu').open(timeout_s=15) as link:
            run(Control(link), args.elf_sha256, report)
    except Exception as error:
        report['error'] = repr(error)
        raise
    finally:
        args.report.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
