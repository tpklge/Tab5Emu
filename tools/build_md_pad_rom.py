#!/usr/bin/env python3
# SPDX-License-Identifier: 0BSD
"""Own 68000 six-button TH-handshake fixture, plus a lossless SMD copy."""
import struct,hashlib,json
from pathlib import Path

def build():
    rom=bytearray(32768);code=bytearray();labels={};fix=[]
    def words(s):code.extend(bytes.fromhex(s))
    def imm(value,address):words('13fc');code.extend(struct.pack('>HI',value,address))
    def move(op,address):words(op);code.extend(struct.pack('>I',address))
    def branch(op,name):fix.append((len(code),name));words(op+'0000')
    words('46fc2700');imm(1,0xa130f1)
    for i,b in enumerate(b'T56'):imm(b,0x200001+2*i)
    imm(64,0xa10009);imm(64,0xa10003)
    labels['loop']=len(code)
    # DBRA provides more than 26 emulated scanlines for pad timeout.
    words('3e3c1388');labels['delay']=len(code);branch('51cf','delay')
    imm(0,0xa10003);move('1039',0xa10003) # D0=Start/A, first low
    imm(64,0xa10003);imm(0,0xa10003)
    imm(64,0xa10003);imm(0,0xa10003);move('1239',0xa10003) # third low ID
    imm(64,0xa10003);move('1439',0xa10003) # D2=XYZ/Mode
    imm(0,0xa10003);move('1639',0xa10003) # fourth low ID
    # Snapshot only while Start is down; release does not overwrite it.
    words('08000005');branch('6600','loop')
    for op,off in [('13c0',8),('13c1',10),('13c2',12),('13c3',14)]:move(op,0x200001+off)
    branch('6000','loop')
    for off,name in fix:struct.pack_into('>h',code,off+2,labels[name]-(off+2))
    struct.pack_into('>II',rom,0,0xfffffc,0x200);rom[0x100:0x110]=b'SEGA MEGA DRIVE '
    rom[0x120:0x150]=b'TAB5 SIX BUTTON TEST - ORIGINAL 0BSD'.ljust(48)
    rom[0x150:0x180]=rom[0x120:0x150];rom[0x1b0:0x1b4]=b'RA\xf8\x20'
    struct.pack_into('>II',rom,0x1b4,0x200001,0x203fff);rom[0x1f0:0x1f3]=b'USA'
    rom[0x200:0x200+len(code)]=code
    struct.pack_into('>H',rom,0x18e,sum(struct.unpack('>'+('H'*((len(rom)-512)//2)),rom[512:]))&65535)
    smd=bytearray(512);smd[1]=3;smd[8:10]=b'\xaa\xbb'
    for off in range(0,len(rom),16384):smd.extend(rom[off+1:off+16384:2]);smd.extend(rom[off:off+16384:2])
    out=Path('artifacts/test-roms');out.mkdir(parents=True,exist_ok=True);report={}
    for ext,data in [('md',rom),('smd',smd)]:
        path=out/('tab5-md-pad6.'+ext);path.write_bytes(data);report[ext]={'file':path.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    report.update(license='0BSD',source='tools/build_md_pad_rom.py',expected_snapshot_hex='1310701f')
    (out/'tab5-md-pad6-manifest.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':print(json.dumps(build(),indent=2))
