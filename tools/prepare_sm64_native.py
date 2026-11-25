#!/usr/bin/env python3
"""Prepare private native SM64 assets from the user's matching ROM.
Requires Python Pillow and the ESP-IDF RISC-V tools on PATH. No ROM downloads.
"""
import argparse, hashlib, os, shlex, shutil, subprocess, sys
from pathlib import Path
from stage_sm64_native import REV, ROM_SHA, ROOT

def prepare(rom, checkout):
    rom, checkout = rom.resolve(), checkout.resolve()
    if hashlib.sha256(rom.read_bytes()).hexdigest()!=ROM_SHA:
        raise ValueError('Expected matching SM64 US ROM; original left untouched')
    if not checkout.exists():
        bundled = ROOT/'third_party/sm64-native-reference'
        source = str(bundled) if (bundled/'.git').exists() else 'https://github.com/DynaMight1124/retro-go'
        subprocess.run(['git','clone','--no-checkout',source,str(checkout)],check=True)
        subprocess.run(['git','-C',str(checkout),'checkout','--detach',REV],check=True)
    if subprocess.check_output(['git','-C',str(checkout),'rev-parse','HEAD'],text=True).strip()!=REV:
        raise ValueError('Use the pinned upstream checkout')
    engine=checkout/'sm64-go/components/sm64-funkey'
    target=engine/'baserom.us.z64'
    if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest()!=ROM_SHA:
        raise ValueError('Existing local baserom differs; not overwritten')
    if not target.exists():shutil.copyfile(rom,target)
    env=dict(os.environ);cc=shlex.split(env.get('CC','cc'))
    sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
    if sys.platform=='darwin' and sdk.exists():cc+=['-isysroot',str(sdk)];env['SDKROOT']=str(sdk)
    def run(args,**kw):return subprocess.run(args,cwd=engine,env=env,check=True,**kw)
    run(['make','-C','tools','n64graphics','mio0','skyconv','aifc_decode','aiff_extract_codebook','vadpcm_enc','CC='+shlex.join(cc+['-include','stdio.h'])])
    run([sys.executable,'extract_assets.py','us'])
    for aiff in engine.glob('sound/samples/**/*.aiff'):
        rel=aiff.relative_to(engine);dest=engine/'build/us'/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        table=dest.with_suffix('.table');aifc=dest.with_suffix('.aifc')
        with table.open('wb') as out:run(['tools/aiff_extract_codebook',str(aiff)],stdout=out)
        run(['tools/vadpcm_enc','-c',str(table),str(aiff),str(aifc)])
    run([sys.executable,'tools/assemble_sound.py','build/us/sound/samples/','sound/sound_banks/','build/us/sound/sound_data.ctl','build/us/sound/sound_data.tbl','-DVERSION_US','--endian','little','--bitwidth','32'])
    seq=engine/'build/us/sound/sequences';seq.mkdir(parents=True,exist_ok=True)
    run(['riscv32-esp-elf-as','-I','include','--defsym','VERSION_US=1','-o',str(seq/'00_sound_player.o'),'sound/sequences/00_sound_player.s'])
    run(['riscv32-esp-elf-objcopy','-j','.rodata',str(seq/'00_sound_player.o'),'-O','binary',str(seq/'00_sound_player.m64')])
    files=sorted(str(p) for p in (engine/'sound/sequences/us').glob('*.m64'))
    run([sys.executable,'tools/assemble_sound.py','--sequences','build/us/sound/sequences.bin','build/us/sound/bank_sets','sound/sound_banks/','sound/sequences.json',*files,str(seq/'00_sound_player.m64'),'-DVERSION_US','--endian','little','--bitwidth','32'])
    for name in ('sound_data.ctl','sound_data.tbl','sequences.bin','bank_sets'):
        data=(engine/'build/us/sound'/name).read_bytes()
        (engine/'build/us/sound'/(name+'.inc.c')).write_text(','.join(hex(v) for v in data))
    # Text preprocessing is host-only. Do not require an unrelated Xtensa SDK.
    source=(engine/'prepare_esp32_assets.py').read_text().replace('"xtensa-esp-elf-gcc"',repr(cc[0]))
    script=engine/'prepare_tab5.local.py';script.write_text(source)
    run([sys.executable,str(script)])
    print('Private local assets prepared. Build sm64-native; do not distribute its payloads.')
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--rom',type=Path,required=True);parser.add_argument('--checkout',type=Path,default=ROOT/'.tools/sm64-native-reference')
    args=parser.parse_args();prepare(args.rom,args.checkout)
