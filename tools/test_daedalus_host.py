#!/usr/bin/env python3
"""Compile the real pinned Daedalus decoder/native backend with ASan/UBSan."""
import concurrent.futures
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from stage_daedalus import SOURCES, stage

ROOT=Path(__file__).resolve().parents[1]
def main():
 os.chdir(ROOT)
 with tempfile.TemporaryDirectory(prefix='tab5-daedalus-test-') as tmp:
  work=Path(tmp);hle=stage(work/'stage')
  flags=shlex.split(os.environ.get('CXX','c++'))+['-std=c++23','-O2','-fsanitize=address,undefined','-fno-omit-frame-pointer','-fwrapv',
    '-DDAEDALUS_ENDIAN_LITTLE=1','-DDAEDALUS_SOFTWARE','-Wno-nontrivial-memcall',
    '-Icomponents/daedalus/include','-Icomponents/daedalus/compat','-I'+str(hle),'-Iexperiments/daedalus/glm']
  if sys.platform=='darwin':
   sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
   flags+=['-isysroot',str(sdk) if sdk.exists() else subprocess.check_output(['xcrun','--show-sdk-path'],text=True).strip()]
  def compile_one(item):
   i,source=item;obj=work/(str(i)+'.o');subprocess.run(flags+['-c',str(source),'-o',str(obj)],check=True);return str(obj)
  with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
   objects=list(pool.map(compile_one,enumerate([hle/name for name in SOURCES]+[ROOT/'components/daedalus/src/software.cpp',ROOT/'tests/test_daedalus.cpp'])))
  test=work/'test';subprocess.run(flags+objects+['-o',str(test)],check=True)
  env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1')
  result=subprocess.run([str(test)],env=env,text=True,capture_output=True)
  if result.returncode:
   sys.stderr.write(result.stdout+result.stderr);result.check_returncode()
  (ROOT/'artifacts/host-daedalus-tests.txt').write_text(result.stdout)
  print(result.stdout,end='')
if __name__=='__main__':main()
