#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Original N64 cart: IPL3, Joybus/EEPROM, PI/AI DMA and software RDP fill.

No Nintendo IPL/BIOS/logo or commercial game data. A changes green to red;
the EEPROM stores an incrementing boot counter followed by the marker N64.
"""
import hashlib
import json
import math
import struct
from pathlib import Path


def build():
    words=[];labels={};branches=[]
    def emit(value):words.append(value&0xffffffff)
    def li(r,value):emit(0x3c000000|(r<<16)|(value>>16&65535));emit(0x34000000|(r<<21)|(r<<16)|(value&65535))
    def sw(r,address):li(1,address);emit(0xac000000|(1<<21)|(r<<16))
    def store(value,address):li(2,value);sw(2,address)
    def load(r,address,byte=False):li(1,address);emit((0x90000000 if byte else 0x8c000000)|(1<<21)|(r<<16))
    def branch(op,s,t,label):branches.append((len(words),label));emit((op<<26)|(s<<21)|(t<<16));emit(0)
    def label(name):labels[name]=len(words)
    def joybuf(address,entries):
        for i in range(0,64,4):store(0,address+i)
        for i,value in enumerate(entries):store(value,address+i*4)
        store(1,address+60)
    def joy(address):
        store(address&0x1fffffff,0xa4800000)
        store(0x1fc007c0,0xa4800010) # SI write RDRAM -> PIF
        store(0x1fc007c0,0xa4800004) # SI read PIF -> RDRAM
        store(0,0xa4800018)          # acknowledge SI interrupt
    # Compare written equal to Count fires only at the next 32-bit wrap.
    li(2,0);emit(0x40824800);emit(0x40825800)
    # Read EEPROM block zero, then write boot counter/marker in a separate transaction.
    joybuf(0x80003100,[0,0x02080400,0,0,0xfe000000]);joy(0x80003100)
    load(16,0x80003108,True);li(3,255);branch(5,16,3,'existing')
    li(16,0)
    label('existing');emit(0x26100001);emit(0x321000ff)
    joybuf(0x80003100,[0,0x0a010500,0,0,0x00fe0000])
    emit((16<<16)|(2<<11)|(24<<6));li(3,0x004e3634);emit((2<<21)|(3<<16)|(2<<11)|0x25);sw(2,0x80003108)
    joy(0x80003100)
    # A genuine PI DMA loads our own stereo tone from cartridge storage.
    store(0x5000,0xa4600000);store(0x10001000,0xa4600004);store(3199,0xa460000c)
    store(0x5000,0xa4500000);store(1013,0xa4500010);store(1,0xa4500008)
    # RDP command list: color image, scissor, fill cycle/color/rectangle, full sync.
    commands=[0xff10013f,0x00100000,0xed000000,0x005003c0,0xef300000,0,
              0xf7000000,0x07c107c1,0xf64fc3bc,0,0xe9000000,0]
    for i,value in enumerate(commands):store(value,0x80002000+i*4)
    store(2,0xa4400000);store(0x100000,0xa4400004);store(320,0xa4400008)
    store(0x006c02ec,0xa4400024);store(0x00250205,0xa4400028);store(1024,0xa4400034)
    label('wait')
    load(4,0xa4300008);emit(0x30840008);branch(4,4,0,'wait')
    store(0,0xa4400010)
    joybuf(0x80003000,[0x01040100,0x000000fe]);joy(0x80003000)
    load(4,0x80003003,True);emit(0x30840080);branch(4,4,0,'green')
    store(0xf801f801,0x8000201c);branch(4,0,0,'render')
    label('green');store(0x07c107c1,0x8000201c)
    label('render')
    store(0x2000,0xa4100000);store(0x2030,0xa4100004)
    store(3200,0xa4500004);store(0,0xa450000c)
    # Capture Cause's IP7 so the host catches a spurious Count/Compare event.
    emit(0x40056800);emit(0x30a58000);sw(5,0x80003f00)
    branch(4,0,0,'wait')
    for offset,target in branches:
        delta=labels[target]-offset-1
        if not -32768<=delta<=32767:raise ValueError('MIPS branch range')
        words[offset]|=delta&65535
    code=struct.pack('>'+str(len(words))+'I',*words)
    if len(code)>0xfc0:raise ValueError('IPL3 fixture too large')
    rom=bytearray(8192);rom[:4]=bytes.fromhex('80371240');struct.pack_into('>I',rom,8,0x80000400)
    rom[0x20:0x34]=b'TAB5 N64 DIAGNOSTIC '.ljust(20)
    rom[0x3b:0x3f]=b'NT5E';rom[0x40:0x40+len(code)]=code
    tone=b''.join(struct.pack('>hh',int(math.sin(i*2*math.pi*440/48009)*4000),int(math.sin(i*2*math.pi*440/48009)*4000)) for i in range(800))
    rom[0x1000:0x1000+len(tone)]=tone
    return bytes(rom)


if __name__=='__main__':
    data=build();sha=hashlib.sha256(data).hexdigest();root=Path('artifacts/test-roms');root.mkdir(parents=True,exist_ok=True)
    name=f'tab5-n64-console-{sha[:12]}.z64';(root/name).write_bytes(data)
    manifest={'license':'0BSD','source':'tools/build_n64_console_rom.py','bytes':len(data),'sha256':sha,'file':name,
              'behavior':'IPL3 -> EEPROM boot counter, PI/AI tone DMA, RDP fill green/red through Joybus A, VI presentation'}
    (root/'tab5-n64-console-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
