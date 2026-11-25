#!/usr/bin/env python3
import os,subprocess,tempfile,sys
from pathlib import Path
from stage_n64_audio import stage,SOURCES
ROOT=Path(__file__).resolve().parents[1]
def main():
 os.chdir(ROOT)
 with tempfile.TemporaryDirectory(prefix='n64-audio-test-') as d:
  folder=Path(d);source=stage(folder/'stage')
  command=['cc','-O2','-std=c11','-fwrapv','-fno-strict-aliasing','-fsanitize=address,undefined','-Icomponents/n64/include','-I'+str(source)]
  if sys.platform=='darwin':command+=['-isysroot','/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk']
  subprocess.run(command+['components/n64/src/n64_audio_hle.c','tests/test_n64_audio_hle.c']+[str(source/name) for name in SOURCES]+['-lm','-o',str(folder/'test')],check=True)
  result=subprocess.run([str(folder/'test')],env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'),text=True,capture_output=True)
  sys.stdout.write(result.stdout);sys.stderr.write(result.stderr);result.check_returncode()
  Path('artifacts/host-n64-audio-hle-tests.txt').write_text(result.stdout)
if __name__=='__main__':main()
