#!/usr/bin/env python3
"""Stage exact pinned ABI1 DSP with explicit RAM/buffer spans and task bounds."""
import argparse,hashlib,json,re,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SOURCES=('alist.c','alist_audio.c','audio.c','memory.c')
def stage(output):
 src=ROOT/'experiments/n64/plugins/audio-hle';output=Path(output)
 manifest=json.loads((src/'UPSTREAM.json').read_text())
 for name,sha in manifest['files'].items():
  if hashlib.sha256((src/name).read_bytes()).hexdigest()!=sha:raise ValueError('Audio HLE source changed: '+name)
 shutil.copytree(src/'src',output,dirs_exist_ok=True)
 p=output/'memory.h';s=p.read_text().replace('#define MEMORY_H','#define MEMORY_H\n#include "n64_audio_hle.h"')
 s=s.replace('buffer + (address ^ S8)','tab5_n64_audio_pointer(buffer,address ^ S8,1)').replace('buffer + (address ^ S16)','tab5_n64_audio_pointer(buffer,address ^ S16,2)').replace('buffer + address','tab5_n64_audio_pointer(buffer,address,4)')
 s=s.replace('assert((address & 1) == 0);','if(address & 1)tab5_n64_audio_abort();').replace('assert((address & 3) == 0);','if(address & 3)tab5_n64_audio_abort();')
 p.write_text(s)
 p=output/'alist.c';s=p.read_text().replace('#include "alist.h"','#include "alist.h"\n#include "n64_audio_hle.h"')
 s=s.replace('while (alist != alist_end) {','while (alist != alist_end) {\n        tab5_n64_audio_step();')
 s=s.replace('hle->dram + address','tab5_n64_audio_pointer(hle->dram,address,1)')
 # Validate complete spans before routines form/advance native sample pointers.
 checks={
 'alist_load':'tab5_n64_audio_span(dmem&~3u,align(count,8),false);tab5_n64_audio_span(address&~7u,align(count,8),true);',
 'alist_save':'tab5_n64_audio_span(dmem&~3u,align(count,8),false);tab5_n64_audio_span(address&~7u,align(count,8),true);',
 'alist_interleave':'tab5_n64_audio_span(dmemo,count*2u,false);tab5_n64_audio_span(left,count,false);tab5_n64_audio_span(right,count,false);',
 'alist_mix':'tab5_n64_audio_span(dmemo,count,false);tab5_n64_audio_span(dmemi,count,false);',
 'alist_envmix_exp':'tab5_n64_audio_span(dmemi,align(count,16),false);tab5_n64_audio_span(dmem_dl,align(count,16),false);tab5_n64_audio_span(dmem_dr,align(count,16),false);if(aux){tab5_n64_audio_span(dmem_wl,align(count,16),false);tab5_n64_audio_span(dmem_wr,align(count,16),false);}tab5_n64_audio_span(address,80,true);',
 'alist_envmix_ge':'tab5_n64_audio_span(dmemi,align(count,16),false);tab5_n64_audio_span(dmem_dl,align(count,16),false);tab5_n64_audio_span(dmem_dr,align(count,16),false);if(aux){tab5_n64_audio_span(dmem_wl,align(count,16),false);tab5_n64_audio_span(dmem_wr,align(count,16),false);}tab5_n64_audio_span(address,80,true);',
 'alist_adpcm':'tab5_n64_audio_span(dmemo,count+32u,false);tab5_n64_audio_span(dmemi,(count/32u)*9u,false);tab5_n64_audio_span(last_frame_address,32,true);if(loop)tab5_n64_audio_span(loop_address,32,true);',
 'alist_resample':'if(dmemi<8)tab5_n64_audio_abort();tab5_n64_audio_span(dmemo,count,false);tab5_n64_audio_span(dmemi-8,(((uint64_t)(count/2)*pitch)>>16)*2+32,false);tab5_n64_audio_span(address,16,true);',
 'alist_polef':'tab5_n64_audio_span(dmemo,count,false);tab5_n64_audio_span(dmemi,count,false);tab5_n64_audio_span(address,8,true);',
 }
 for name,check in checks.items():
  pattern=r'(void '+name+r'\([^{}]+\)\s*\{)'
  s,n=re.subn(pattern,lambda m:m[0]+'\n    '+check,s)
  if n!=1:raise ValueError('Audio adapter function changed: '+name)
 # Masked per-sample helpers remain bounded even at unusual command offsets.
 s=s.replace('(int16_t*)hle->alist_buffer + ((pos ^ S) & 0xfff)','(int16_t*)tab5_n64_audio_pointer(hle->alist_buffer,((pos ^ S)&0xfff)*2,2)')
 s=s.replace('assert((count & 0x1f) == 0);','if(count & 0x1f)tab5_n64_audio_abort();')
 s=s.replace('const int16_t* const cb_entry = codebook + ((code & 0xf) << 4);','if((code & 0xf)>=8)tab5_n64_audio_abort();\n        const int16_t* const cb_entry = codebook + ((code & 0xf) << 4);')
 p.write_text(s)
 p=output/'alist_audio.c';s=p.read_text().replace('    dram_load_u16(hle, (uint16_t*)hle->alist_audio.table, address, align(count, 8) >> 1);','    if (align(count,8)>sizeof(hle->alist_audio.table))tab5_n64_audio_abort();\n    dram_load_u16(hle, (uint16_t*)hle->alist_audio.table, address, align(count, 8) >> 1);');p.write_text(s)
 return output
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('output',type=Path);stage(p.parse_args().output)
