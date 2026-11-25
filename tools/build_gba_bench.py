#!/usr/bin/env python3
"""Build the 0BSD ARM/Thumb SMC benchmark; no downloaded ROMs or BIOS."""
import os,shutil,subprocess,json,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def checksum():
    n=0x13579bdf
    for i in range(4096):
        n=((n<<5)|(n>>27))&0xffffffff;n^=(i*0x9e3779b9)&0xffffffff
        n=(n+0x1020304)&0xffffffff;n=(n*1664525)&0xffffffff;n^=n>>13
    return n

def build():
    gcc=shutil.which('arm-none-eabi-gcc') or str(Path.home()/'.platformio/packages/toolchain-gccarmnoneeabi/bin/arm-none-eabi-gcc')
    prefix=os.getenv('TAB5_ARM_PREFIX',gcc.removesuffix('gcc'))
    out=ROOT/'artifacts/test-roms';out.mkdir(parents=True,exist_ok=True)
    elf=out/'tab5-rv32-bench.elf'
    subprocess.run([prefix+'gcc','-mcpu=arm7tdmi','-marm','-O2','-ffreestanding','-fno-builtin','-nostdlib','-Wl,--build-id=none','-T',str(ROOT/'tests/homebrew/gba/link.ld'),str(ROOT/'tests/homebrew/gba/start.S'),str(ROOT/'tests/homebrew/gba_bench/main.c'),'-lgcc','-o',str(elf)],check=True)
    image=out/'tab5-rv32-bench.gba';subprocess.run([prefix+'objcopy','-O','binary',str(elf),str(image)],check=True)
    data=bytearray(image.read_bytes());data[0xa0:0xac]=b'TAB5 RV32   ';data[0xac:0xb0]=b'T5RV';data[0xb0:0xb2]=b'00';data[0xb2]=0x96;data[0xbd]=(-sum(data[0xa0:0xbd])-0x19)&255
    data.extend(bytes(max(0,32768-len(data))));image.write_bytes(data)
    report={'license':'0BSD','file':image.name,'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data),'alu_checksum':checksum(),'smc_expected_failures':0,'source':'tests/homebrew/gba_bench'}
    (out/'tab5-rv32-bench-manifest.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':print(json.dumps(build(),indent=2))
