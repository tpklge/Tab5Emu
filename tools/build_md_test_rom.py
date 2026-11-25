#!/usr/bin/env python3
"""Original 0BSD 68000/VDP/SRAM fixture; no console BIOS or game data."""
import hashlib
import json
import struct
from pathlib import Path


def build():
    rom = bytearray(32768)
    code = bytearray()
    labels, fixups = {}, []

    def emit(words):
        code.extend(bytes.fromhex(words))

    def absolute(opcode, address):
        emit(opcode)
        code.extend(struct.pack('>I', address))

    def immediate(opcode, value, address, long=False):
        emit(opcode)
        code.extend(struct.pack('>I' if long else '>H', value))
        code.extend(struct.pack('>I', address))

    def branch(opcode, target):
        fixups.append((len(code), target))
        emit(opcode + '0000')

    emit('46fc2700')  # SR: supervisor, interrupts masked
    immediate('13fc', 1, 0xa130f1)  # SRAM enabled
    immediate('0c39', 0x54, 0x200001)
    branch('6600', 'initialize')
    immediate('0c39', 0x35, 0x200003)
    branch('6600', 'initialize')
    absolute('1039', 0x200005)
    emit('5200')  # addq.b #1,d0
    branch('6000', 'store')
    labels['initialize'] = len(code)
    immediate('13fc', 0x54, 0x200001)
    immediate('13fc', 0x35, 0x200003)
    emit('7001')
    labels['store'] = len(code)
    absolute('13c0', 0x200005)
    absolute('13c0', 0xff0000)
    # Blank backdrop with a counter encoded in its red component. Planes
    # remain disabled, making a deterministic native-frame oracle.
    for value in (0x8004, 0x8104, 0x8700, 0x8c81, 0x8f02):
        immediate('33fc', value, 0xc00004)
    immediate('13fc', 0x40, 0xa10009)  # TH as output
    immediate('13fc', 0, 0xa10003)  # TH low: Start on bit 5
    emit('7400')  # no previous Start press
    labels['loop'] = len(code)
    absolute('1239', 0xa10003)
    emit('02010020')  # andi.b #$20,d1
    branch('6600', 'released')
    emit('4a02')  # tst.b d2
    branch('6600', 'draw')
    absolute('5239', 0xff0000)
    absolute('1039', 0xff0000)
    absolute('13c0', 0x200005)
    emit('7401')
    branch('6000', 'draw')
    labels['released'] = len(code)
    emit('7400')
    labels['draw'] = len(code)
    emit('7000')
    absolute('1039', 0xff0000)
    emit('02400007e34800400220')  # (counter&7)<<1 | green
    immediate('23fc', 0xc0000000, 0xc00004, long=True)
    absolute('33c0', 0xc00000)
    branch('6000', 'loop')
    for offset, target in fixups:
        struct.pack_into('>h', code, offset + 2, labels[target] - (offset + 2))
    struct.pack_into('>II', rom, 0, 0x00fffffc, 0x200)
    rom[0x100:0x110] = b'SEGA MEGA DRIVE '
    title = b'TAB5 SRAM DIAGNOSTIC - ORIGINAL 0BSD'.ljust(48)
    rom[0x120:0x150] = title
    rom[0x150:0x180] = title
    rom[0x1b0:0x1b4] = b'RA\xf8\x20'
    struct.pack_into('>II', rom, 0x1b4, 0x200001, 0x203fff)
    rom[0x1f0:0x1f3] = b'USA'
    rom[0x200:0x200+len(code)] = code
    checksum = sum(struct.unpack('>'+'H'*((len(rom)-512)//2), rom[512:])) & 65535
    struct.pack_into('>H', rom, 0x18e, checksum)
    return bytes(rom), bytes(code)


if __name__ == '__main__':
    data, code = build()
    digest = hashlib.sha256(data).hexdigest()
    root = Path('artifacts/test-roms'); root.mkdir(parents=True, exist_ok=True)
    path = root / ('tab5-md-sram-' + digest[:12] + '.md')
    path.write_bytes(data)
    path.with_suffix('.json').write_text(json.dumps({
        'license': '0BSD', 'author': 'Tab5-Emu contributors',
        'sha256': digest, 'bytes': len(data), 'code_bytes': len(code),
        'sram_bytes': 8192, 'behavior': 'Boot/Start increment byte 2 of odd-lane SRAM; backdrop red encodes counter & 7.'
    }, indent=2)+'\n')
    print(path)
