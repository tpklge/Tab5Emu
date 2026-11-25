#!/usr/bin/env python3
"""Build a private N64 scene runner with the same staged CPU/RSP/RDP as firmware."""
import argparse
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile


def build(output, daedalus=False, sanitize=False, console_tests=False, audio_hle=False, unity=False):
    output=Path(output).resolve();output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='tab5-n64-host-') as folder:
        work=Path(folder);stage=work/'stage'
        subprocess.run([sys.executable,'tools/stage_n64.py',str(stage)],check=True)
        sources=[stage/'device/r4300'/n for n in ('pure_interp.c','cp1.c','cp2.c')]
        sources += [stage/'rsp'/n for n in ('su.c','vu/vu.c','vu/multiply.c','vu/add.c','vu/select.c','vu/logical.c','vu/divide.c')]
        sources += [stage/'rdp/n64video.c',Path('components/n64/src/n64_console.c'),Path('components/n64/src/n64_plugins.c')]
        sources += [Path('experiments/n64/mupen/src/device/pif')/n for n in ('cic.c','n64_cic_nus_6105.c')]
        flags=shlex.split(os.environ.get('CC','cc'))+['-O3','-std=c11','-fwrapv','-fno-strict-aliasing','-DNO_ASM','-DUSE_CLIENT_ENDIAN','-DPLUGIN_API_VERSION=0x0101']
        if sys.platform=='darwin':
            sdk=os.environ.get('SDKROOT')
            if not sdk:
                default=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk')
                sdk=str(default) if default.exists() else subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()
            flags+=['-isysroot',sdk]
        for include in (stage,stage/'device/r4300',stage/'rsp',stage/'rdp','experiments/n64/mupen/src','experiments/n64/mupen/src/device/r4300','experiments/n64/mupen/subprojects/xxhash','components/n64/include'):
            flags+=['-I',str(include)]
        if sanitize: flags += ['-fsanitize=address,undefined','-fno-omit-frame-pointer']
        if daedalus:
            from stage_daedalus import stage as stage_hle, SOURCES
            hle=stage_hle(work/'daedalus')
            flags+=['-DTAB5_N64_DAEDALUS','-Icomponents/daedalus/include']
        if audio_hle:flags+=['-DTAB5_N64_AUDIO_HLE']
        if unity:flags+=['-DTAB5_N64_FAST_CPU']
        def compile_one(item):
            number,source=item;obj=work/(str(number)+'.o')
            subprocess.run(flags+['-c',str(source),'-o',str(obj)],check=True)
            return str(obj)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            objects=list(pool.map(compile_one,enumerate(sources)))
        if audio_hle:
            from stage_n64_audio import stage as stage_audio, SOURCES as AUDIO_SOURCES
            audio=stage_audio(work/'audio-hle')
            audio_flags=flags+['-DTAB5_N64_AUDIO_HLE','-I'+str(audio)]
            def compile_audio(item):
                number,source=item;obj=work/('audio-'+str(number)+'.o')
                subprocess.run(audio_flags+['-c',str(source),'-o',str(obj)],check=True);return str(obj)
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                objects+=list(pool.map(compile_audio,enumerate([audio/name for name in AUDIO_SOURCES]+[Path('components/n64/src/n64_audio_hle.c')])))
        main_obj=work/'main.o'
        subprocess.run(flags+['-c','tests/test_n64_console.c' if console_tests else 'tests/n64_host_main.c','-o',str(main_obj)],check=True)
        objects.append(str(main_obj))
        linker=shlex.split(os.environ.get('CC','cc'))
        if daedalus:
            cpp=shlex.split(os.environ.get('CXX','c++'))+[x for x in flags[len(shlex.split(os.environ.get('CC','cc'))):] if x!='-std=c11']
            cpp+=['-std=c++23','-DDAEDALUS_ENDIAN_LITTLE=1','-DDAEDALUS_SOFTWARE',
                   '-Wno-nontrivial-memcall','-Icomponents/daedalus/compat','-I'+str(hle),'-Iexperiments/daedalus/glm']
            def compile_cpp(item):
                number,source=item;obj=work/('hle-'+str(number)+'.o')
                subprocess.run(cpp+['-c',str(source),'-o',str(obj)],check=True)
                return str(obj)
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                objects+=list(pool.map(compile_cpp,enumerate([hle/name for name in SOURCES]+[Path('components/daedalus/src/software.cpp')])))
            linker=shlex.split(os.environ.get('CXX','c++'))
        linkflags=['-lm']+(['-fsanitize=address,undefined'] if sanitize else [])
        if sys.platform=='darwin':linkflags+=['-isysroot',sdk]
        subprocess.run(linker+objects+linkflags+['-o',str(output)],check=True)
    return output


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--audio-hle',action='store_true');parser.add_argument('--unity',action='store_true');parser.add_argument('--console-tests',action='store_true');parser.add_argument('--daedalus',action='store_true');parser.add_argument('--sanitize',action='store_true')
    args=parser.parse_args();os.chdir(Path(__file__).resolve().parents[1]);print(build(args.output,args.daedalus,args.sanitize,args.console_tests,args.audio_hle,args.unity))
