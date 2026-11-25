#!/usr/bin/env python3
"""Gate app4 size and interpreter-only build flags; never install or flash."""
import argparse
import hashlib
import json
from pathlib import Path
from package import check_image

RESERVATION = 4063232

def check(build):
    build = Path(build)
    data = check_image(build / 'tab5-emu.bin')
    if len(data) > RESERVATION or ((len(data) + 65535) & ~65535) != RESERVATION:
        raise ValueError('app-only image would exceed or change the existing app4 reservation')
    if 'TAB5_PROFILE:STRING=stable\n' not in (build / 'CMakeCache.txt').read_text():
        raise ValueError('expected the stable profile')
    commands = json.loads((build / 'compile_commands.json').read_text())
    gba = [c for c in commands if '/components/gba/' in c['file'].replace('\\', '/')]
    if not gba:
        raise ValueError('GBA build evidence missing')
    for entry in gba:
        command = entry.get('command', ' '.join(entry.get('arguments', [])))
        if ('/rv32/' in entry['file'].replace('\\', '/') or
                any(flag in command for flag in ('-DHAVE_DYNAREC', '-DTAB5_GBA_RV32', '-DTAB5_GBA_HEAP_CHECK'))):
            raise ValueError('stable contains experimental GBA or heap diagnostic code')
    return {'profile': 'stable', 'image_bytes': len(data), 'reservation_bytes': RESERVATION,
            'sha256': hashlib.sha256(data).hexdigest(), 'jit_compiled': False,
            'app_only': True, 'hardware_tested': False}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path); parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(); result = check(args.build)
    args.report.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
