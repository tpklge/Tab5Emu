#!/usr/bin/env python3
"""Check the refused legacy updater command on a known, corrected firmware."""
import argparse
import json
import time
from pathlib import Path
from emulator_usb import prepare


def check(control, expected_elf, expected_version='0.1.15'):
    from emulink.control import ControlError
    info = control.info()
    # Never probe this mutating legacy command on an older firmware.
    if expected_version not in ('0.1.14','0.1.15') or info.get('firmware') != expected_version or info.get('elf_sha256') != expected_elf:
        raise RuntimeError('firmware/ELF differs; legacy command was not sent')
    before = control.status()
    try:
        control.enter_updater()
    except ControlError as error:
        message = str(error)
        if 'Launcher do Paludo' not in message:
            raise
    else:
        raise RuntimeError('legacy updater command was accepted')
    time.sleep(.5)
    after = control.status()
    assert after['uptime_ms'] > before['uptime_ms']
    assert after['state'] == before['state']
    assert after['rom'] == before['rom']
    assert control.info()['elf_sha256'] == expected_elf
    if before['state'] == 'game' and not before['paused']:
        assert after['game_frames'] > before['game_frames']
    return {'firmware': info['firmware'], 'elf_sha256': expected_elf,
            'state': before['state'], 'error': message,
            'uptime_before_ms': before['uptime_ms'], 'uptime_after_ms': after['uptime_ms'],
            'same_firmware_and_game': True, 'no_reset': True,
            'game_progress': before['state'] == 'game' and not before['paused']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--expected-elf', required=True)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    if args.report.exists():
        raise FileExistsError(args.report)
    prepare()
    from emulink.link import Link
    from emulink.control import Control
    with Link('tab5-emu').open(timeout_s=20) as link:
        result = check(Control(link), args.expected_elf)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
