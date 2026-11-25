#!/usr/bin/env python3
"""Original 0BSD 68000 EEPROM fixtures, with no game/BIOS data."""
import hashlib
import json
import struct
from pathlib import Path


def build(board='sega'):
    code = bytearray()
    labels, branches, calls = {}, [], []
    scl, sda, output = (6, 7, 7) if board == 'ea' else (1, 0, 1 if board == 'acclaim16' else 0)
    def emit(hexcode): code.extend(bytes.fromhex(hexcode))
    def imm(op, value, address, long=False):
        emit(op); code.extend(struct.pack('>I' if long else '>H', value)); code.extend(struct.pack('>I', address))
    def absolute(op, address): emit(op); code.extend(struct.pack('>I', address))
    def branch(op, target): branches.append((len(code), target)); emit(op+'0000')
    def call(target): calls.append((len(code)+2, target)); emit('4eb900000000')
    def pins(c, d): imm('13fc', (c<<scl)|(d<<sda), 0x200001)
    def start(): pins(0, 1); pins(1, 1); pins(1, 0); pins(0, 0)
    def stop(): pins(0, 0); pins(1, 0); pins(1, 1)
    def send(value):
        for n in range(7, -1, -1):
            d = (value>>n)&1; pins(0, d); pins(1, d); pins(0, d)
        pins(0, 1); pins(1, 1); pins(0, 1)
    def address(read=False):
        start()
        if board == 'acclaim16':
            send(0xa0); send(0)
            if read: start(); send(0xa1)
        else: send(int(read))

    emit('46fc2700')
    call('increment')
    for value in (0x8004, 0x8104, 0x8700, 0x8c81, 0x8f02): imm('33fc', value, 0xc00004)
    imm('13fc', 0x40, 0xa10009); imm('13fc', 0, 0xa10003); emit('7400')
    labels['loop'] = len(code)
    absolute('1239', 0xa10003); emit('02010020'); branch('6600', 'released')
    emit('4a02'); branch('6600', 'draw'); call('increment'); emit('7401'); branch('6000', 'draw')
    labels['released'] = len(code); emit('7400')
    labels['draw'] = len(code)
    emit('7000'); absolute('1039', 0xff0000); emit('02400007e34800400220')
    imm('23fc', 0xc0000000, 0xc00004, True); absolute('33c0', 0xc00000); branch('6000', 'loop')
    labels['increment'] = len(code)
    address(True); emit('7000')
    for n in range(8):
        emit('d000'); pins(1, 1); absolute('1239', 0x200001)
        emit('0801'); code.extend(struct.pack('>H', output)); branch('6700', f'read{n}')
        emit('5200'); labels[f'read{n}'] = len(code); pins(0, 1)
    pins(0, 1); pins(1, 1); pins(0, 1); stop()  # NACK
    emit('0c0000ff'); branch('6600', 'existing'); emit('7000')
    labels['existing'] = len(code); emit('5200'); absolute('13c0', 0xff0000)
    address(False)
    for n in range(8):
        emit('d000'); branch('6400', f'zero{n}')
        pins(0, 1); pins(1, 1); pins(0, 1); branch('6000', f'bit{n}')
        labels[f'zero{n}'] = len(code); pins(0, 0); pins(1, 0); pins(0, 0)
        labels[f'bit{n}'] = len(code)
    pins(0, 1); pins(1, 1); pins(0, 1)
    send(ord('T')); send(ord('5')); send(('sega', 'ea', 'acclaim16').index(board)+1); stop(); emit('4e75')
    for off, target in branches: struct.pack_into('>h', code, off+2, labels[target]-(off+2))
    for off, target in calls: struct.pack_into('>I', code, off, 512+labels[target])
    rom = bytearray(32768)
    struct.pack_into('>II', rom, 0, 0xfffffc, 512)
    rom[0x100:0x110] = b'SEGA MEGA DRIVE '
    title = ('TAB5 EEPROM '+board.upper()+' - ORIGINAL 0BSD').encode().ljust(48)
    rom[0x120:0x150] = title; rom[0x150:0x180] = title
    product = {'sega': b'GM MK-1215 -00', 'ea': b'GM T-50176-00', 'acclaim16': b'GM T-081326-00'}[board]
    # Product identifiers exercise board detection; no original game content.
    rom[0x180:0x18e] = product.ljust(14); rom[0x1b0:0x1b4] = b'RA\xe8\x20'
    struct.pack_into('>II', rom, 0x1b4, 0x200001, 0x200001)
    rom[0x1f0:0x1f3] = b'USA'; rom[512:512+len(code)] = code
    struct.pack_into('>H', rom, 0x18e, sum(struct.unpack('>'+str((len(rom)-512)//2)+'H', rom[512:]))&65535)
    return bytes(rom)


if __name__ == '__main__':
    root = Path('artifacts/test-roms'); root.mkdir(parents=True, exist_ok=True)
    manifest = {'license': '0BSD', 'author': 'Tab5-Emu contributors', 'boards': {}}
    for board in ('sega', 'ea', 'acclaim16'):
        data = build(board); digest = hashlib.sha256(data).hexdigest()
        name = f'tab5-md-eeprom-{board}-{digest[:12]}.md'; (root/name).write_bytes(data)
        manifest['boards'][board] = {'file': name, 'sha256': digest, 'bytes': len(data)}
    (root/'tab5-md-eeprom-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    print(json.dumps(manifest, indent=2))
