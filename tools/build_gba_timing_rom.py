#!/usr/bin/env python3
"""Build the headless, MIT-licensed mGBA timing subset without a BIOS/logo."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import argparse

ROOT=Path(__file__).resolve().parents[1]
def build(output):
    gcc=shutil.which('arm-none-eabi-gcc') or str(Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-gcc')
    prefix=os.getenv('TAB5_ARM_PREFIX',gcc.removesuffix('gcc'))
    source=ROOT/'tests/homebrew/gba_timing';vendor=source/'mgba-suite'
    manifest=json.loads((vendor/'UPSTREAM.json').read_text())
    for name,sha in manifest['files'].items():
        if hashlib.sha256((vendor/name).read_bytes()).hexdigest()!=sha:
            raise ValueError('Pinned mGBA timing source changed: '+name)
    output.mkdir(parents=True,exist_ok=True)
    objects=[]
    for name in ('basic','loadstore','mul','b'):
        obj=output/('timing-'+name+'.o')
        subprocess.run([prefix+'gcc','-mcpu=arm7tdmi','-mthumb-interwork','-x','assembler-with-cpp',
            '-I',str(vendor/'include'),'-c',str(vendor/'src/tests'/(name+'.s')),'-o',str(obj)],check=True)
        # libgba's link normally supplies these executable RAM section flags.
        # This standalone runner has no libgba startup or linker scripts.
        subprocess.run([prefix+'objcopy','--set-section-flags','.iwram=alloc,load,readonly,code,contents',
            '--set-section-flags','.ewram=alloc,load,readonly,code,contents',str(obj)],check=True)
        objects.append(str(obj))
    elf=output/'tab5-gba-timing.elf';image=output/'tab5-gba-timing.gba'
    subprocess.run([prefix+'gcc','-mcpu=arm7tdmi','-marm','-mthumb-interwork','-O2','-ffreestanding','-fno-builtin',
        '-nostdlib','-Wl,--build-id=none','-T',str(source/'link.ld'),str(ROOT/'tests/homebrew/gba/start.S'),
        str(source/'main.c'),*objects,'-lgcc','-o',str(elf)],check=True)
    subprocess.run([prefix+'objcopy','-O','binary',str(elf),str(image)],check=True)
    data=bytearray(image.read_bytes());data[0xa0:0xac]=b'TAB5 TIMING ';data[0xac:0xb0]=b'T5TM';data[0xb0:0xb2]=b'00';data[0xb2]=0x96
    data[0xbd]=(-sum(data[0xa0:0xbd])-0x19)&255;data.extend(bytes((-len(data))%32768));image.write_bytes(data)
    report={'license':'runner 0BSD; imported assembly and timing expectations MIT',
        'reference_commit':manifest['commit'],'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),
        'cases':json.loads((source/'expected.json').read_text()),'boot':'game; proprietary logo absent',
        'sram_layout':{'magic':'T5TM','version':4,'case_count':8,'mode_count':12,'completed_cases':16,
            'data_offset':256,'case_bytes':240,'baseline_offset':0,'observed_offset':80,'relative_offset':160}}
    (output/'tab5-gba-timing-manifest.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,default=ROOT/'artifacts/test-roms')
    r=build(p.parse_args().output);print(json.dumps({k:v for k,v in r.items() if k!='cases'},indent=2))
