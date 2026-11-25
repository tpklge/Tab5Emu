#!/usr/bin/env python3
"""Optional native raster checks with original geometry and ASan/UBSan."""
import os,subprocess,tempfile
from pathlib import Path
from stage_sm64_native import ROOT,stage
with tempfile.TemporaryDirectory(prefix='tab5-sm64-raster-') as work:
 work=Path(work);stage(ROOT/'.tools/sm64-native-reference',work)
 engine=ROOT/'.tools/sm64-native-reference/sm64-go/components/sm64-funkey'
 flags=['cc','-O2','-std=gnu11','-fno-strict-aliasing','-fsanitize=address,undefined','-DVERSION_US','-DENABLE_SOFTRAST','-D_LANGUAGE_C','-DF3DEX_GBI_2E','-DCONVERT','-DAVOID_UB=1','-DNON_MATCHING=1','-DRETRO_GO=1','-DTARGET_FUNKEY=1','-DNO_SEGMENTED_MEMORY=1']
 sdk=Path('/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk')
 if sdk.exists():flags+=['-isysroot',str(sdk)]
 for p in [ROOT/'components/sm64_native/compat',engine/'include',engine/'src',engine/'src/pc/gfx',engine]:flags+=['-I',str(p)]
 subprocess.run(flags+[str(work/'gfx_soft.c'),str(engine/'src/pc/gfx/gfx_cc.c'),str(ROOT/'tests/test_sm64_raster.c'),'-lm','-o',str(work/'test')],check=True)
 output=''
 for w,h in [(160,120),(128,96),(80,60)]:
  result=subprocess.run([str(work/'test'),str(w),str(h)],env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'),capture_output=True,text=True)
  if result.returncode:print(result.stdout+result.stderr);result.check_returncode()
  output+=result.stdout
 (ROOT/'artifacts/host-sm64-raster-tests.txt').write_text(output);print(output,end='')
