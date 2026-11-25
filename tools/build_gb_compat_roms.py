#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Own MBC3 RTC and MBC2 nibble/mirror/bank diagnostics, direct boot only."""
from build_8bit_test_roms import Program
from pathlib import Path
import json,hashlib

def finish(p,kind,ram,bank_count=2):
    rom=bytearray(16384*bank_count);rom[0x100:0x104]=bytes.fromhex('C3 50 01 00')
    rom[0x134:0x143]=b'TAB5 COMPAT'.ljust(15,b'\0');rom[0x147]=kind;rom[0x148]=0;rom[0x149]=ram
    code=p.finish();rom[0x150:0x150+len(code)]=code
    check=0
    for v in rom[0x134:0x14d]:check=(check-v-1)&255
    rom[0x14d]=check
    return rom

def rtc():
    p=Program();p.emit('F3 31 FF DF AF E0 40 3E 0A EA 00 00 AF EA 00 40')
    p.emit('FA 00 A0 FE 52');p.branch(0x28,'loop')
    # Halt while setting 511 days, 23:59:59, then resume.
    for register,value in [(12,0x41),(8,59),(9,59),(10,23),(11,255),(12,1)]:
        p.emit(f'3E {register:02x} EA 00 40 3E {value:02x} EA 00 A0')
    p.emit('AF EA 00 40 3E 52 EA 00 A0')
    p.label('loop');p.emit('AF EA 00 60 3E 01 EA 00 60')
    for n in range(5):p.emit(f'3E {8+n:02x} EA 00 40 FA 00 A0 EA {n:02x} C0')
    p.emit('AF EA 00 40')
    for n in range(5):p.emit(f'FA {n:02x} C0 EA {8+n:02x} A0')
    p.branch(0x18,'loop')
    return finish(p,0x10,2)

def mbc2():
    p=Program();p.emit('F3 31 FF DF AF E0 40 3E 0A EA 00 00')
    # A200 mirrors A000. Upper bits are deliberately nonzero on write.
    p.emit('3E AB EA 00 A2 FA 00 A0 E6 0F EA 01 A0')
    # Bank number zero aliases bank one, including writes at $0100.
    p.emit('AF EA 00 01 FA 00 40 E6 0F EA 02 A0')
    p.emit('FA FF A1 E6 0F 3C EA FF A1')
    p.label('idle');p.branch(0x18,'idle')
    rom=finish(p,6,0);rom[0x4000]=7
    return rom

if __name__=='__main__':
    root=Path('artifacts/test-roms');root.mkdir(parents=True,exist_ok=True);report={}
    for name,builder in [('rtc',rtc),('mbc2',mbc2)]:
        data=builder();path=root/f'tab5-gb-{name}.gb';path.write_bytes(data)
        report[name]={'license':'0BSD','file':path.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    (root/'tab5-gb-compat-manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
