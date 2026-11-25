#!/usr/bin/env python3
"""Import explicit legacy saves into Tab5-Emu's ROM-SHA namespaces.

Dry run by default. Originals are never modified. Run with the emulator stopped
and the SD mounted on the computer; --root is the mounted Tab5-Emu directory.
"""
import argparse
import hashlib
import json
import os
import struct
from pathlib import Path

SYSTEMS = ('gb', 'gbc', 'gba', 'snes', 'genesis')
MAX_SAVE = 2 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path, limit=MAX_SAVE):
    path = Path(path)
    with path.open('rb') as f:
        data = f.read(limit + 1)
    if not data or len(data) > limit:
        raise ValueError(f'Arquivo vazio ou acima do limite: {path}')
    return data


def u32(data, off=0):
    return struct.unpack_from('<I', data, off)[0]


def gb_geometry(rom):
    if len(rom) < 0x150 or rom[0x148] > 8 or len(rom) < (32768 << rom[0x148]) or rom[0x149] > 5:
        raise ValueError('Cabecalho/bancos GB/GBC invalidos')
    supported = (0, 1, 2, 3, 5, 6, 8, 9, 15, 16, 17, 18, 19, 25, 26, 27, 28, 29, 30, 254, 255)
    if rom[0x147] not in supported:
        raise ValueError('Mapper GB/GBC ainda nao suportado')
    return (1, 1, 1, 4, 16, 4)[rom[0x149]]


def md_rom(rom):
    if len(rom) >= 512 and rom[1] == 3 and rom[8:10] == b'\xaa\xbb':
        if not (len(rom)-512) or (len(rom)-512) % 16384:
            raise ValueError('SMD truncado')
        decoded = bytearray(len(rom)-512)
        for off in range(0, len(decoded), 16384):
            block = rom[512+off:512+off+16384]
            decoded[off:off+16384:2] = block[8192:]
            decoded[off+1:off+16384:2] = block[:8192]
        rom = bytes(decoded)
    if len(rom) < 512 or len(rom) > 0x800000 or len(rom) % 2:
        raise ValueError('ROM Mega Drive invalida')
    pc = int.from_bytes(rom[4:8], 'big')
    if pc < 512 or pc >= len(rom) or pc % 2:
        raise ValueError('Vetor de boot Mega Drive invalido')
    return rom


def md_geometry(rom):
    rom = md_rom(rom)
    has_ram = rom[0x1b0:0x1b2] == b'RA'
    first, last = struct.unpack_from('>II', rom, 0x1b4)
    linear = has_ram and rom[0x1b2] != 0xe8 and last > first+1
    code = rom[0x180:0x18e].decode('ascii', errors='replace')
    if code.startswith('GM'):
        code = code[2:]
    code = code.lstrip(' ').split('\0')[0].strip().split(' ')[0]
    ids = ('T-50176', 'T-50396', 'T-50446', 'T-50516', 'T-50606', 'T-12046', 'T-12053',
           'MK-1215', 'MK-1228', 'G-5538', 'PR-1993', 'G-4060', '00001211', '00004076', 'G-4524', '00054503')
    match = lambda key: code == key or code.startswith(key+'-')
    if not linear:
        if any(map(match, ids)):
            return 128
        if match('T-81033') or match('T-081326'):
            return 256
    if not has_ram:
        return 0
    if rom[0x1b2] == 0xe8 or first >= last or last >= 0x800000:
        raise ValueError('EEPROM/mapper Mega Drive nao identificado')
    size = (last-first)//(2 if first % 2 == last % 2 else 1)+1
    if size > 131072:
        raise ValueError('SRAM Mega Drive acima do limite')
    return size


def battery_data(system, rom, data, gba_backup=None, snes_layout=None):
    if system in ('gb', 'gbc'):
        size = gb_geometry(rom)*8192
        if rom[0x147] not in (3, 6, 9, 15, 16, 19, 27, 30, 254, 255):
            raise ValueError('Cartucho GB/GBC sem bateria persistente')
        if rom[0x147] in (5, 6) and len(data) in (512, 8192):
            # gnuboy keeps its legacy 8 KiB container; MBC2 has 512 nibbles.
            return bytes(b & 15 for b in data[:512]) + bytes([255])*(8192-512)
        if len(data) == 2048 and rom[0x149] == 1:
            return data + bytes([255])*(size-len(data))
    elif system == 'gba':
        if len(rom) < 192 or len(rom) > 32*1024*1024:
            raise ValueError('ROM GBA truncada/acima do limite')
        size = 131072
        formats = {'eeprom512': (512, (b'EEPROM_V',)), 'eeprom8k': (8192, (b'EEPROM_V',)),
                   'sram32k': (32768, (b'SRAM_V', b'SRAM_F_V')),
                   'flash64k': (65536, (b'FLASH_V', b'FLASH512_V')),
                   'flash128k': (131072, (b'FLASH1M_V',))}
        if gba_backup:
            count, signatures = formats[gba_backup]
            if len(data) != count or not any(s in rom for s in signatures):
                raise ValueError('Tamanho/tipo de backup GBA nao corresponde a ROM e a opcao explicita')
            return data + bytes([255])*(size-len(data))
    elif system == 'snes':
        if snes_layout not in ('lorom', 'hirom'):
            raise ValueError('Informe --snes-layout lorom ou hirom para a SRAM SNES')
        if len(rom) % 32768 == 512:
            rom = rom[512:]
        off = 0x7fc0 if snes_layout == 'lorom' else 0xffc0
        maps = (0x20, 0x30) if snes_layout == 'lorom' else (0x21, 0x31)
        if len(rom) < off+64 or rom[off+21] not in maps or not 1 <= rom[off+24] <= 7:
            raise ValueError('Somente SRAM SNES LoROM/HiROM convencional identificada e suportada')
        size = 1 << (rom[off+24]+10)
    else:
        size = md_geometry(rom)
    if not size or len(data) != size:
        raise ValueError(f'Tamanho de bateria incompativel: {len(data)}; esperado {size}')
    return data


def rtc_data(system, rom, data):
    if system not in ('gb', 'gbc') or len(rom) < 0x150 or rom[0x147] not in (15, 16):
        raise ValueError('RTC importavel somente para cartuchos MBC3 com relogio')
    gb_geometry(rom)
    if len(data) not in (32, 40):
        raise ValueError('RTC deve ser snapshot Tab5 v1 (32 bytes) ou v2 (40 bytes)')
    magic, day, hour, minute, second, tick, halt, carry = struct.unpack_from('<8I', data)
    version = 1 if len(data) == 32 else 2
    if magic != (0x31544352 if version == 1 else 0x32544352) or day > (1023 if version == 1 else 511) or hour > 23 or minute > 59 or second > 59 or tick > 59 or halt > 1 or carry > 1:
        raise ValueError('Campos de RTC invalidos; RTC anexado por outro emulador exige conversao especifica')
    return f'rtc-v{version}.bin'


def gb_state(rom, data):
    banks = gb_geometry(rom)
    color = bool(rom[0x143] & 0x80)
    iram, vram = (8, 4) if color else (2, 2)
    if len(data) != (1+iram+vram+banks*2)*4096:
        raise ValueError('Tamanho de estado gnuboy incompativel')
    wanted = {b'GbSs': 0x105, b'cgb ': int(color), b'iram': 1, b'vram': 1+iram,
              b'sram': 1+iram+vram, b'hi  ': 3328, b'pal ': 3584, b'oam ': 3840, b'wav ': 3312}
    found = set()
    for off in range(0, 4096, 8):
        tag, value = bytes(data[off:off+4]), u32(data, off+4)
        if tag == bytes(4):
            if found == wanted.keys():
                return data
            break
        if tag in wanted:
            if tag in found or value != wanted[tag]:
                break
            found.add(tag)
        if tag == b'hram' and value:
            break
    raise ValueError('Layout de estado gnuboy incompativel; loader legado nao sera acionado')


def bson_schema(data, depth=0):
    if depth > 8 or len(data) < 5 or u32(data) != len(data) or data[-1]:
        raise ValueError('Documento BSON invalido')
    pos, schema, seen = 4, [], set()
    while pos < len(data)-1:
        kind = data[pos]
        end = data.find(b'\0', pos+1, min(pos+257, len(data)-1))
        if end < 0:
            raise ValueError('Chave BSON sem terminacao')
        key = bytes(data[pos+1:end])
        if not key or key in seen:
            raise ValueError('Chave BSON vazia/duplicada')
        seen.add(key)
        pos = end+1
        if pos+4 > len(data)-1:
            raise ValueError('Valor BSON truncado')
        size = u32(data, pos)
        if kind == 16:
            detail = size if key in (b'info-magic', b'info-version') else None
            pos += 4
        elif kind in (3, 4):
            if size < 5 or pos+size > len(data)-1:
                raise ValueError('Documento BSON fora dos limites')
            detail = bson_schema(data[pos:pos+size], depth+1)
            pos += size
        elif kind == 5:
            if pos+5+size > len(data)-1 or data[pos+4] != 0:
                raise ValueError('Bloco BSON fora dos limites/subtipo desconhecido')
            detail = size
            pos += 5+size
        else:
            raise ValueError('Tipo BSON nao suportado pelo gpSP')
        schema.append((key, kind, detail))
    if pos != len(data)-1:
        raise ValueError('Terminacao BSON invalida')
    return tuple(schema)


def md_records(data):
    pos, schema = 0, []
    while pos < len(data):
        if len(data)-pos < 32:
            raise ValueError('Registro Gwenesis truncado')
        header = data[pos:pos+32]
        size = u32(header, 28)
        if not size or size > len(data)-pos-32:
            raise ValueError('Registro Gwenesis fora dos limites')
        schema.append(header)
        pos += 32+size
    if not schema:
        raise ValueError('Estado Gwenesis vazio')
    return tuple(schema)


def md_state(data, digest, ram_size):
    if len(data) < 112 or data[:8] != b'T5MDST01':
        raise ValueError('Esperado estado Tab5 Mega Drive com cabecalho')
    version, header = struct.unpack_from('<II', data, 8)
    if version not in (1, 2, 3, 4) or header != (144 if version == 4 else 112):
        raise ValueError('Versao de estado Mega Drive nao suportada')
    ram, control, core = struct.unpack_from('<III', data, 48)
    if data[16:48].hex() != digest or ram != ram_size or control > 3 or len(data) != header+core+ram:
        raise ValueError('Estado pertence a outra ROM ou geometria')
    checked = data[:60]+bytes(32)+data[92:]
    if hashlib.sha256(checked).digest() != data[60:92]:
        raise ValueError('SHA do estado Mega Drive invalido')
    if version >= 2:
        if data[92:96] != b'PAD6':
            raise ValueError('Estado do controle invalido')
        for off in (96, 100, 104):
            a, b, c, d = data[off:off+4]
            if a > 8 or a % 2 or b > 26 or c > 15 or d > 1:
                raise ValueError('Fase de controle invalida')
    elif any(data[92:112]):
        raise ValueError('Campos reservados de estado v1 invalidos')
    if version == 3:
        if data[108:111] != b'TK\1' or data[111] > 15:
            raise ValueError('Latch Tekken invalido')
    elif any(data[108:112]):
        raise ValueError('Campos reservados invalidos')
    if version == 4:
        b = data[112:144]
        if b[:3] != b'EE\1' or b[3] not in (1, 2, 3) or ram != (256 if b[3] == 3 else 128) or any(x > 1 for x in b[4:7]) or b[7] > 6 or b[8] > 6 or b[9] > 8 or b[11] >= ram or b[12] > 2 or b[13] > 1 or b[14] >= ram or b[14] % 4 or b[15] > 15 or any(b[20:]) or (b[7] == 4 and b[9] == 8 and not b[4]) or (b[7] in (1, 2, 3) and b[9] > 7) or (b[7] == 6 and b[8] not in (2, 3, 4)):
            raise ValueError('Estado de EEPROM invalido')
    return md_records(data[header:header+core])


def state_data(system, rom, data, template, battery=None):
    if system in ('gb', 'gbc'):
        return gb_state(rom, data)
    if system == 'snes':
        raise ValueError('Estado SNES raw contem estruturas dependentes da build; importe a bateria, nao este estado')
    if template is None:
        raise ValueError('Informe --template: estado novo salvo pelo firmware atual com esta mesma ROM')
    if system == 'gba':
        if len(data) != 416*1024 or len(template) != len(data):
            raise ValueError('Estado gpSP de outra versao/tamanho')
        schema = bson_schema(data)
        if (b'info-magic', 16, 0x06bac0de) not in schema or (b'info-version', 16, 0x10004) not in schema or schema != bson_schema(template):
            raise ValueError('Esquema gpSP incompativel com o firmware atual')
        return data
    digest, ram = sha(rom), md_geometry(rom)
    expected = md_state(template, digest, ram)
    if data.startswith(b'T5MDST01'):
        if md_state(data, digest, ram) != expected or (u32(data, 8) in (3, 4) or u32(template, 8) in (3, 4)) and u32(data, 8) != u32(template, 8):
            raise ValueError('Esquema/mapper Mega Drive incompativel')
        if u32(data, 8) == 4 and data[115] != template[115]:
            raise ValueError('Placa de EEPROM difere do template atual')
        return data
    if u32(template, 8) not in (1, 2):
        raise ValueError('Estado raw nao guarda latch Tekken ou transacao EEPROM; conversao recusada')
    if md_records(data) != expected:
        raise ValueError('Registros raw Gwenesis incompativeis com o firmware atual')
    if ram and battery is None:
        raise ValueError('Estado raw sem SRAM: informe --battery com a bateria antiga correspondente')
    tail = battery_data('genesis', rom, battery) if ram else b''
    header = bytearray(template[:112])
    struct.pack_into('<II', header, 8, 2, 112)
    struct.pack_into('<I', header, 56, len(data))
    header[60:92] = bytes(32)
    header[92:96] = b'PAD6'
    for off in (96, 100, 104):
        header[off:off+3] = bytes((0, 0, 15))
    header[108:112] = bytes(4)
    result = header+data+tail
    result[60:92] = hashlib.sha256(result).digest()
    md_state(result, digest, ram)
    return bytes(result)


def verify_source_journal(source, data):
    base = str(source)
    if base.endswith('.bak'):
        base = base[:-4]
    seen = False
    for suffix in ('.sha256', '.sha256.bak'):
        journal = Path(base+suffix)
        if not journal.exists():
            continue
        seen = True
        try:
            text = read(journal, 256).decode('ascii')
        except (ValueError, UnicodeError):
            continue
        lines = text.splitlines()
        if text.endswith('\n') and 2 <= len(lines) <= 4 and lines[0] == f'T5B1 {len(data)}' and all(len(x) == 64 and all(c in '0123456789abcdef' for c in x) for x in lines[1:]) and sha(data) in lines[1:]:
            return
    if seen:
        raise ValueError('Sidecar existente nao valida o save de origem')


def prepare(args):
    if args.system not in SYSTEMS or args.kind not in ('battery', 'state', 'rtc'):
        raise ValueError('Sistema/tipo de save invalido')
    rom = read(args.rom, 64*1024*1024)
    original = read(args.source)
    verify_source_journal(args.source, original)
    if args.kind == 'battery':
        data = battery_data(args.system, rom, original, args.gba_backup, args.snes_layout)
        relative = Path('saves')/args.system/sha(rom)/'battery.sav'
    elif args.kind == 'rtc':
        filename = rtc_data(args.system, rom, original)
        data = original
        relative = Path('saves')/args.system/sha(rom)/filename
    else:
        if args.slot is None or not 0 <= args.slot <= 5:
            raise ValueError('Estado exige --slot entre 0 e 5')
        data = state_data(args.system, rom, original, read(args.template) if args.template else None,
                          read(args.battery) if args.battery else None)
        stem = Path(args.rom).stem
        if not stem or any(ord(c) < 32 or c in '\\:' or ord(c) == 127 for c in stem):
            raise ValueError('Nome de ROM invalido')
        relative = Path('states')/args.system/sha(rom)/'core-v1'/f'{stem}_{args.slot}_{args.system}.sav'
    root = Path(args.root).absolute()
    if root.is_symlink():
        raise ValueError('Raiz de destino e link simbolico')
    root = root.resolve()
    destination = root/relative
    # Check all ancestors without resolving away a symlink first.
    for parent in (destination, *destination.parents):
        if parent.is_symlink():
            raise ValueError(f'Destino contem link simbolico: {parent}')
        if parent == root:
            break
    if not root.is_dir() or len(relative.as_posix().encode()) > 240:
        raise ValueError('Raiz inexistente ou caminho acima do limite do firmware')
    suffixes = ('', '.bak', '.tmp', '.rejected', '.sha256', '.sha256.bak', '.sha256.tmp', '.migration.json', '.migration-lock')
    targets = [destination]
    if args.kind == 'rtc':
        # v2 takes precedence over v1 at boot; do not report an import that
        # would silently be ignored, or replace a clock on another timeline.
        targets = [destination.parent/'rtc-v1.bin', destination.parent/'rtc-v2.bin']
    for target in targets:
        for suffix in suffixes:
            candidate = Path(str(target)+suffix)
            if candidate.exists() or candidate.is_symlink():
                raise FileExistsError(f'Destino ocupado; escolha outro slot/raiz: {candidate}')
    plan = {'format': 'tab5-save-migration-v1', 'system': args.system, 'kind': args.kind,
            'rom': str(Path(args.rom).resolve()), 'rom_sha256': sha(rom),
            'source': str(Path(args.source).resolve()), 'source_bytes': len(original), 'source_sha256': sha(original),
            'destination': str(destination), 'bytes': len(data), 'sha256': sha(data),
            'identity': 'ROM selected explicitly; a legacy file without ROM hash cannot prove its original game',
            'original_preserved': True}
    for key in ('template', 'battery'):
        path = getattr(args, key)
        if path:
            plan[key] = {'path': str(Path(path).resolve()), 'sha256': sha(read(path))}
    return destination, data, plan


def migrate(args):
    destination, data, plan = prepare(args)
    plan['applied'] = False
    if not args.apply:
        return plan
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation works on FAT too. Journal first: an interrupted copy
    # cannot become an accepted unverified legacy battery at the next boot.
    created = []
    def write(path, content):
        with path.open('xb') as f:
            created.append(path)
            f.write(content)
            f.flush()
            os.fsync(f.fileno())
    lock = Path(str(destination)+'.migration-lock')
    try:
        write(lock, b'Tab5-Emu save migration in progress\n')
        if args.kind in ('battery', 'rtc'):
            write(Path(str(destination)+'.sha256'), f'T5B1 {len(data)}\n{sha(data)}\n'.encode())
        write(destination, data)
        if read(destination) != data:
            raise OSError('Conferencia do destino falhou')
        plan['applied'] = True
        write(Path(str(destination)+'.migration.json'), (json.dumps(plan, indent=2, ensure_ascii=False)+'\n').encode())
    except BaseException:
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    lock.unlink()
    return plan


def parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True, help='Diretorio Tab5-Emu do SD montado')
    p.add_argument('--system', choices=SYSTEMS, required=True)
    p.add_argument('--kind', choices=('battery', 'state', 'rtc'), required=True)
    p.add_argument('--rom', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--slot', type=int)
    p.add_argument('--template', type=Path, help='Estado criado no firmware atual com esta ROM (GBA/MD)')
    p.add_argument('--battery', type=Path, help='Bateria da mesma epoca de um estado raw MD')
    p.add_argument('--gba-backup', choices=('eeprom512', 'eeprom8k', 'sram32k', 'flash64k', 'flash128k'))
    p.add_argument('--snes-layout', choices=('lorom', 'hirom'))
    p.add_argument('--apply', action='store_true', help='Gravar copia; sem esta opcao apenas simula')
    return p


def main():
    args = parser().parse_args()
    try:
        print(json.dumps(migrate(args), indent=2, ensure_ascii=False))
    except (OSError, ValueError, struct.error) as error:
        raise SystemExit(f'Migracao recusada: {error}') from error


if __name__ == '__main__':
    main()
