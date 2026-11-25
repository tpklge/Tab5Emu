#!/usr/bin/env python3
"""Gate the N64 console profile and the unchanged Launcher app4 reservation."""
import argparse
import hashlib
import json
from pathlib import Path
from package import check_image
from check_stable_artifact import RESERVATION


def check(build):
    build=Path(build);data=check_image(build/'tab5-emu.bin')
    if len(data)>RESERVATION or (len(data)+65535)&~65535 != RESERVATION:
        raise ValueError('N64 image would change/exceed the app4 reservation')
    if 'TAB5_PROFILE:STRING=n64\n' not in (build/'CMakeCache.txt').read_text():
        raise ValueError('Expected the N64 console profile')
    commands=json.loads((build/'compile_commands.json').read_text())
    required={'n64_console.c','n64_plugins.c','pure_interp.c','su.c','n64video.c'}
    built=set()
    for entry in commands:
        name=Path(entry['file']).name;command=entry.get('command',' '.join(entry.get('arguments',[])))
        if '/rv32/' in entry['file'] or any(flag in command for flag in ('-DHAVE_DYNAREC','-DTAB5_GBA_RV32','-DNEW_DYNAREC')):
            raise ValueError('Experimental JIT source/flags in N64 console profile')
        if name in required:built.add(name)
    if built!=required:raise ValueError('Real N64 CPU/RSP/RDP sources missing')
    return {'profile':'n64','image_bytes':len(data),'reservation_bytes':RESERVATION,
            'sha256':hashlib.sha256(data).hexdigest(),'elf_sha256':data[176:208].hex(),
            'jit_compiled':False,'app_only':True,'hardware_tested':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('--report',type=Path,required=True)
    args=p.parse_args();report=check(args.build);args.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
