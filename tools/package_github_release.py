#!/usr/bin/env python3
"""Package the measured app-only delivery; ROM-derived native modules stay local."""
import argparse
import hashlib
import json
import subprocess
import tarfile
import zipfile
from pathlib import Path, PurePosixPath
from package import check_image
from source_bundle import bundle

ROOT=Path(__file__).resolve().parents[1]
VERSION='v0.2.3'
PERSONAL={'roms','bios','saves','states','screenshots','logs','cache','benchmarks'}
ROM_SUFFIXES={'.z64','.n64','.v64','.gba','.gb','.gbc','.sfc','.smc','.iso','.chd'}

def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def validate_source(path):
    with tarfile.open(path,'r:gz') as archive:
        names=set()
        for entry in archive:
            parts=PurePosixPath(entry.name).parts
            if entry.name in names or not entry.isfile() or parts[0]!='Tab5Emu-source' or '..' in parts:
                raise ValueError('Unsafe source archive entry: '+entry.name)
            names.add(entry.name)
            if any(p in ('.git','.tools','ROMs','__pycache__') for p in parts) or '.local.' in entry.name or Path(entry.name).suffix.lower() in ROM_SUFFIXES:
                raise ValueError('Private/ROM file in source archive: '+entry.name)
        required={'LICENSE','NOTICE','SOURCE_MANIFEST.json','components/sm64_native/src/port.c',
                  'third_party/sm64-native-reference/sm64-go/components/sm64-funkey/src/pc/gfx/gfx_soft.c'}
        if not {'Tab5Emu-source/'+p for p in required}<=names:raise ValueError('Incomplete source archive')
        manifest=json.load(archive.extractfile('Tab5Emu-source/SOURCE_MANIFEST.json'))
        expected={'Tab5Emu-source/'+entry['path'] for entry in manifest['files']}
        if expected|{'Tab5Emu-source/SOURCE_MANIFEST.json'}!=names:raise ValueError('Source manifest file set differs')
        for entry in manifest['files']:
            member=archive.getmember('Tab5Emu-source/'+entry['path'])
            if member.size!=entry['size'] or hashlib.file_digest(archive.extractfile(member),'sha256').hexdigest()!=entry['sha256']:
                raise ValueError('Source content differs: '+entry['path'])
    return len(names)

def package(destination,include_source=True):
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    if any(destination.iterdir()):raise ValueError('Use a new empty release directory')
    delivery=json.loads((ROOT/'artifacts/n64-performance-delivery.json').read_text())
    if not delivery['complete']:raise ValueError('Hardware delivery not complete')
    apps=[]
    for key,build,name in [('general_firmware','build-sd-cores','tab5-emu-v0.2.3-perf.bin'),
                           ('native_firmware','build-sm64-native','tab5-sm64-v0.1.0-native.bin')]:
        proof=delivery[key];data=check_image(ROOT/build/(proof['project']+'.bin'),proof['project'])
        if hashlib.sha256(data).hexdigest()!=proof['sha256'] or data[176:208].hex()!=proof['elf_sha256']:
            raise ValueError('Binary differs from tested delivery: '+name)
        (destination/name).write_bytes(data)
        apps.append({'file':name,'project':proof['project'],'version':proof['version'],'sha256':proof['sha256'],
                     'elf_sha256':proof['elf_sha256'],'resources_included':key=='general_firmware'})
    source=ROOT/'sdcard/Tab5-Emu';general=delivery['general_firmware']
    core_names={m['core'] for m in general['cores']}
    if core_names!={'n64','gba','snes','genesis','gbc'}:raise ValueError('Unexpected public cores')
    sd=destination/('Tab5Emu-microSD-'+VERSION+'.zip')
    with zipfile.ZipFile(sd,'x',compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(source.rglob('*')):
            relative=path.relative_to(source);parts=relative.parts
            if path.is_file() and parts and parts[0] in PERSONAL:raise ValueError('Personal SD data: '+str(relative))
            target='Tab5-Emu/'+relative.as_posix()
            if path.is_dir():archive.writestr(target+'/',b'')
            elif path.is_file():archive.write(path,target)
        archive.writestr('Tab5-Emu/config/n64-performance.cfg','1 1 2\n')
        for member in general['cores']:
            path=ROOT/'build-sd-cores/sd/Tab5-Emu/cores'/member['file']
            if sha(path)!=member['sha256']:raise ValueError('Wrong SD core: '+member['core'])
            archive.write(path,'Tab5-Emu/cores/'+member['file'])
        archive.writestr('Tab5-Emu/cores/manifest.json',json.dumps({'format':'tab5-sd-cores-v1','cores':general['cores']},indent=2)+'\n')
        for path in [ROOT/'LICENSE',ROOT/'NOTICE',ROOT/'LICENSES/Tab5Emu-upstream-MIT.txt',ROOT/'docs/PUBLICACAO_GITHUB.md',ROOT/'docs/N64_DESEMPENHO.md',ROOT/'docs/LICENCAS.md']:
            archive.write(path,'Tab5-Emu/docs/'+path.name)
    with zipfile.ZipFile(sd) as archive:
        if archive.testzip():raise ValueError('SD archive CRC failed')
        if any(n.startswith('Tab5-Emu/cores/sm64-') for n in archive.namelist()):raise ValueError('Native ROM module must remain local')
    (destination/'INSTALACAO.md').write_bytes((ROOT/'docs/PUBLICACAO_GITHUB.md').read_bytes())
    (destination/'VERIFICACAO.json').write_text(json.dumps(delivery,indent=2)+'\n')
    source_info=None
    if include_source:
        source_path=destination/('Tab5Emu-source-complete-'+VERSION+'.tar.gz')
        source_info=bundle(source_path,all_components=True);source_info['archive_entries']=validate_source(source_path)
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    manifest={'format':'tab5-emu-github-release-v1','version':VERSION,'source_commit':commit,
              'project_license':'GPL-3.0-or-later','third_party_licenses_preserved':True,
              'apps':apps,'native_rom_module_included':False,'native_local_pair_build_required':True,
              'roms_saves_captures_and_device_dumps_included':False,'app_only':True,
              'bootloader_or_partition_table_included':False,'source_bundle':source_info,
              'files':[{'name':p.name,'bytes':p.stat().st_size,'sha256':sha(p)} for p in sorted(destination.iterdir()) if p.is_file()]}
    (destination/'RELEASE.json').write_text(json.dumps(manifest,indent=2)+'\n')
    (destination/'SHA256SUMS').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in sorted(destination.iterdir()) if p.is_file() and p.name!='SHA256SUMS'))
    for p in destination.iterdir():
        if p.is_file() and p.stat().st_size>=2*1024**3:raise ValueError('GitHub asset limit exceeded')
    return manifest

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--without-source',action='store_true')
    args=p.parse_args();result=package(args.output,not args.without_source)
    print(json.dumps({'version':result['version'],'files':result['files'],'native_rom_module_included':False},indent=2))
