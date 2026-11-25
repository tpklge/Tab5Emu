#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Build our GB/SNES SRAM diagnostics from original machine-code sources.

No commercial ROM, firmware, logo or external assembler is used. The GB
header intentionally omits the proprietary logo: direct emulator boot only.
Each cartridge increments [0x54, counter] in battery RAM exactly once per boot.
"""
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


class Program:
    def __init__(self):self.data=bytearray();self.labels={};self.branches=[]
    def emit(self,hexbytes):self.data.extend(bytes.fromhex(hexbytes))
    def label(self,name):self.labels[name]=len(self.data)
    def branch(self,opcode,target):
        self.branches.append((len(self.data),target));self.data.extend([opcode,0])
    def finish(self):
        for offset,target in self.branches:
            delta=self.labels[target]-offset-2
            if not -128<=delta<=127:raise ValueError('branch out of range')
            self.data[offset+1]=delta&255
        return self.data


def gameboy(color=False):
    p=Program()
    p.emit('F3 31 FF DF AF E0 40')       # DI; SP=$DFFF; LCD off
    if color:
        # CGB BG palette 0: white, black, red, green (RGB555). Original
        # instructions, including auto-increment through BGPI/BGPD.
        p.emit('3E 80 E0 68 3E FF E0 69 3E 7F E0 69 AF E0 69 E0 69')
        p.emit('3E 1F E0 69 AF E0 69 3E E0 E0 69 3E 03 E0 69')
    p.emit('3E 0A EA 00 00')             # MBC1 battery RAM enable
    p.emit('FA 00 A0 FE 54');p.branch(0x20,'first') # signature?
    p.emit('FA 01 A0 3C');p.branch(0x18,'store')   # previous counter + 1
    p.label('first');p.emit('3E 01')
    p.label('store');p.emit('47 EA 01 A0 3E 54 EA 00 A0') # B=count; RAM=[54,count]
    p.emit('21 00 80 0E 08')             # tile 0, eight rows
    p.label('tile');p.emit('78 22 2F 22 0D');p.branch(0x20,'tile')
    p.emit('21 00 98 01 00 04')          # 32x32 BG tile map
    p.label('map');p.emit('AF 22 0B 78 B1');p.branch(0x20,'map')
    p.emit('3E E4 E0 47 AF E0 42 E0 43 3E 91 E0 40') # palette, scroll, LCD on
    p.label('idle');p.branch(0x18,'idle')
    rom=bytearray(32768)
    rom[0x100:0x104]=bytes.fromhex('C3 50 01 00') # JP $0150
    rom[0x134:0x143]=b'TAB5 SRAM TEST'.ljust(15,b'\0')
    rom[0x147]=3                        # MBC1 + RAM + battery
    if color:rom[0x143]=0x80            # CGB-capable cartridge
    rom[0x148]=0;rom[0x149]=2            # 32 KiB ROM, 8 KiB SRAM
    code=p.finish();rom[0x150:0x150+len(code)]=code
    checksum=0
    for value in rom[0x134:0x14d]:checksum=(checksum-value-1)&255
    rom[0x14d]=checksum
    rom[0x14e:0x150]=(sum(rom)&65535).to_bytes(2,'big')
    return rom


def snes():
    p=Program()
    p.emit('78 18 FB C2 10 E2 20 A2 FF 1F 9A 4B AB') # SEI, native, X16/A8, SP, DBR=0
    p.emit('A9 80 8D 00 21 9C 00 42')               # forced blank; IRQ/NMI disabled
    p.emit('AF 00 00 70 C9 54');p.branch(0xd0,'first')
    p.emit('AF 01 00 70 1A');p.branch(0x80,'store')  # SRAM counter + 1
    p.label('first');p.emit('A9 01')
    p.label('store');p.emit('8F 01 00 70 A9 54 8F 00 00 70')
    p.emit('A9 00 8D 21 21 AF 01 00 70 29 1F 8D 22 21') # CGRAM color 0 from counter
    p.emit('A9 00 8D 22 21 A9 0F 8D 00 21')         # color high byte; full brightness
    p.label('idle');p.branch(0x80,'idle')
    rom=bytearray([255])*32768;code=p.finish();rom[:len(code)]=code
    rom[0x7fc0:0x7fd5]=b'TAB5 SRAM DIAGNOSTIC'.ljust(21,b' ')
    rom[0x7fd5:0x7fdc]=bytes([0x20,0x02,5,3,1,0,0]) # LoROM, RAM+batt, 32K ROM/8K RAM, NTSC
    for offset in range(0x7fe0,0x8000,2):rom[offset:offset+2]=(0x8000).to_bytes(2,'little')
    rom[0x7fdc:0x7fe0]=bytes(4)
    checksum=(sum(rom)+510)&65535
    rom[0x7fdc:0x7fde]=(checksum^65535).to_bytes(2,'little')
    rom[0x7fde:0x7fe0]=checksum.to_bytes(2,'little')
    assert sum(rom)&65535==checksum
    return rom


def main():
    destination=ROOT/'artifacts/test-roms';destination.mkdir(parents=True,exist_ok=True)
    for system,builder,extension in [('gb',gameboy,'.gb'),('gbc',lambda:gameboy(True),'.gbc'),('snes',snes,'.sfc')]:
        data=builder();path=destination/('tab5-diagnostic-'+system+extension);path.write_bytes(data)
        result={'system':system,'file':path.name,'size':len(data),'sha256':hashlib.sha256(data).hexdigest(),
                'license':'0BSD','source':'tools/build_8bit_test_roms.py','purpose':'battery SRAM boot counter [0x54, count]'}
        (destination/('manifest-'+system+'.json')).write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result))


if __name__=='__main__':main()
