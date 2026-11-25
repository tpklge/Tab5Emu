#!/usr/bin/env python3
"""Verify app4 bounds and every exact-build SD overlay before installation."""
import argparse,hashlib,json,struct
from pathlib import Path
from check_stable_artifact import RESERVATION
from package import check_image
from sd_core_format import verify


def elf_symbols_and_sections(path):
    data=Path(path).read_bytes()
    if data[:6]!=b'\x7fELF\x01\x01':raise ValueError('Expected ELF32 little endian')
    shoff=struct.unpack_from('<I',data,32)[0];entsize,num,shstr=struct.unpack_from('<HHH',data,46)
    sections=[struct.unpack_from('<10I',data,shoff+i*entsize) for i in range(num)]
    def string(off):return data[off:data.index(b'\0',off)].decode()
    names=[string(sections[shstr][4]+s[0]) for s in sections]
    table=sections[names.index('.symtab')];strings=sections[table[6]][4];symbols={}
    for off in range(table[4],table[4]+table[5],16):
        name,value,_,_,_,_=struct.unpack_from('<IIIBBH',data,off);symbols[string(strings+name)]=value
    return data,sections,symbols


def check(build):
    build=Path(build);image=check_image(build/'tab5-emu.bin')
    if len(image)>RESERVATION or ((len(image)+65535)&~65535)!=RESERVATION:
        raise ValueError(f'SD core image would change app4: {len(image)} bytes')
    if 'TAB5_PROFILE:STRING=sd-cores\n' not in (build/'CMakeCache.txt').read_text():raise ValueError('Expected sd-cores profile')
    commands=json.loads((build/'compile_commands.json').read_text())
    for entry in commands:
        command=entry.get('command',' '.join(entry.get('arguments',[])))
        if '/rv32/' in entry['file'] or any(flag in command for flag in ('-DTAB5_GBA_RV32','-DHAVE_DYNAREC','-DTAB5_GBA_HEAP_CHECK')):
            raise ValueError('Experimental GBA source in SD profile')
    elf,sections,symbols=elf_symbols_and_sections(build/'tab5-emu.elf')
    if hashlib.sha256(elf).digest()!=image[176:208]:raise ValueError('ELF and image differ')
    hle='TAB5_N64_DAEDALUS:BOOL=ON\n' in (build/'CMakeCache.txt').read_text()
    if hle:
        base=symbols['_tab5_ovl_start'];end=base+symbols['_tab5_ovl_n64_size']
        for name in ('tab5_daedalus_init','tab5_daedalus_task','tab5_daedalus_close'):
            if name not in symbols or not base<=symbols[name]<end:
                raise ValueError('Daedalus function missing from N64 SD overlay: '+name)
    folder=build/'sd/Tab5-Emu/cores';manifest=json.loads((folder/'manifest.json').read_text());required={'gba','snes','genesis','gbc','n64'}
    if {m['core'] for m in manifest['cores']}!=required:raise ValueError('Mature or N64 core missing')
    for member in manifest['cores']:
        file=member['file'];core=member['core']
        if Path(file).name!=file or file!=core+'-'+member['sha256']+'.bin':raise ValueError('Invalid SD path')
        payload=(folder/file).read_bytes();address=symbols['_tab5_ovl_'+core+'_lma']
        section=next(s for s in sections if s[1]!=8 and s[2]&2 and s[3]<=address and address+48<=s[3]+s[5])
        offset=section[4]+address-section[3];header=elf[offset:offset+48]
        sha=verify(header,payload,symbols['_tab5_ovl_start'])
        if sha!=member['sha256'] or len(payload)!=member['bytes']:raise ValueError('SD manifest differs')
    return {'profile':'sd-cores','image_bytes':len(image),'reservation_bytes':RESERVATION,'sha256':hashlib.sha256(image).hexdigest(),'elf_sha256':image[176:208].hex(),'cores':manifest['cores'],'n64_graphics':'daedalus-hle-software' if hle else 'cxd4-angrylion-lle','jit_compiled':False,'app_only':True,'hardware_tested':False}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('build',type=Path);p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();result=check(a.build);a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
