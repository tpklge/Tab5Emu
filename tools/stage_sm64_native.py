#!/usr/bin/env python3
"""Stage a local SM64 port; never download or bundle ROM-derived assets."""
import argparse, hashlib, json, re, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
REV = '03271e74ee81105e124904475004e9852c8ff067'
ROM_SHA = '17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91'

def stage(checkout, output):
    checkout, output = Path(checkout).resolve(), Path(output).resolve()
    actual = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
    if actual != REV:
        raise ValueError('Native source revision differs from pinned reference')
    root = checkout / 'sm64-go/components/sm64-funkey'
    if hashlib.sha256((root/'baserom.us.z64').read_bytes()).hexdigest() != ROM_SHA:
        raise ValueError('Native profile requires the matching local US ROM')
    for asset in ('assets/mario_anims.c', 'sound/sound_data.ctl.inc.c', 'sound/sound_data.tbl.inc.c', 'sound/sequences.bin.inc.c'):
        if not (root/asset).is_file():
            raise ValueError('Prepare local assets before building: '+asset)
    output.mkdir(parents=True, exist_ok=True)
    source = (root/'src/pc/ultra_reimplementation.c').read_text()
    source = '#include "tab5_sm64.h"\n' + source
    # Change only EEPROM persistence: the frontend journals the 512-byte save.
    for name, body in (
        ('osEepromLongRead', 'return tab5_sm64_eeprom_read(address, buffer, nbytes);'),
        ('osEepromLongWrite', 'return tab5_sm64_eeprom_write(address, buffer, nbytes);')):
        start = source.index('s32 '+name+'(')
        brace = source.index('{', start)
        depth, end = 1, brace+1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        source = source[:brace+1]+'\n    '+body+'\n}'+source[end:]
    (output/'ultra_reimplementation.c').write_text(source)
    renderer=(root/'src/pc/gfx/gfx_soft.c').read_text()
    for name,value in (('scr_width','SM64_RENDER_WIDTH'),('scr_height','SM64_RENDER_HEIGHT')):
        variable='tab5_sm64_render_'+('width' if name=='scr_width' else 'height')
        original='#define '+name+' '+value
        if renderer.count(original)!=1:raise ValueError('Native raster dimensions changed upstream')
        renderer=renderer.replace(original,'#define '+name+' ((int)'+variable+')')
    renderer=renderer.replace('#define scr_size (SM64_RENDER_WIDTH * SM64_RENDER_HEIGHT)',
        '#define scr_size (scr_width * scr_height)')
    renderer='extern unsigned tab5_sm64_render_width,tab5_sm64_render_height;\n'+renderer
    (output/'gfx_soft.c').write_text(renderer)
    cmake = (root/'CMakeLists.txt').read_text()
    base = re.search(r'set\(srcs\s+(.*?)\n\)', cmake, re.S)[1]
    files = [root/name for name in re.findall(r'"([^\"]+\.c)"', base)]
    files = [p for p in files if p.name not in ('ultra_reimplementation.c','fsutils.c','savestate.c','gfx_sdl.c')]
    files = [output/'gfx_soft.c' if p.name=='gfx_soft.c' else p for p in files]
    for pattern in ('src/goddard/*.c','src/goddard/dynlists/*.c','src/menu/*.c','src/buffers/*.c','actors/**/*.c','levels/**/*.c','bin/**/*.c','textures/skyboxes/*_skybox.c'):
        files += sorted(p for p in root.glob(pattern) if not p.name.endswith('.inc.c') and '/bin/eu/' not in str(p) and '/bin/jp/' not in str(p))
    files.append(output/'ultra_reimplementation.c')
    includes = [root/p for p in ('include','src','src/pc','src/pc/gfx','src/game','src/audio','src/engine','src/buffers','.')]
    cmake_list = lambda name, paths: 'set('+name+'\n'+''.join('  "'+str(p)+'"\n' for p in paths)+')\n'
    (output/'sources.cmake').write_text(cmake_list('sm64_sources',files)+cmake_list('sm64_includes', includes))
    (output/'local-assets.json').write_text(json.dumps({'upstream': REV,'rom_sha256':ROM_SHA,'private_assets':True,'sources':len(files)}, indent=2)+'\n')
if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('checkout',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();stage(args.checkout,args.output)
