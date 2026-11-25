#!/usr/bin/env python3
"""Keep the pinned APU intact; place its working state in internal RAM on load."""
import hashlib
from pathlib import Path
import argparse

PIN = 'a210f3fb3efc5133b5f5e3e9e6dd30db964c8665f16ec9548f6eb8759dd89baa'

def stage(source, output):
    source=Path(source);text=source.read_text()
    if hashlib.sha256(source.read_bytes()).hexdigest()!=PIN:
        raise ValueError('Snes9x APU source changed; audit the memory adapter')
    text=text.replace('#include "../copyright"', '#include "'+str((source.parent.parent/'copyright').resolve())+'"\n#include <esp_heap_caps.h>\n#include <stdio.h>')
    text=text.replace('static spc_state_t m;', 'static spc_state_t *tab5_spc_state;\n#define m (*tab5_spc_state)')
    text=text.replace('bool S9xInitAPU()\n{', '''bool S9xInitAPU()
{
   if (!tab5_spc_state) {
      if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) >= sizeof(*tab5_spc_state) + 64 * 1024)
         tab5_spc_state = heap_caps_calloc(1, sizeof(*tab5_spc_state), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      int internal = tab5_spc_state != NULL;
      if (!tab5_spc_state)
         tab5_spc_state = heap_caps_calloc(1, sizeof(*tab5_spc_state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!tab5_spc_state) return false;
      printf("snes: SPC working state %u bytes, internal=%d\\n", (unsigned)sizeof(*tab5_spc_state), internal);
   }''')
    text=text.replace('void S9xDeinitAPU()\n{', '''void S9xDeinitAPU()
{
   heap_caps_free(tab5_spc_state);
   tab5_spc_state = NULL;''')
    # These operations intentionally select/wrap 32-bit bit patterns. Make
    # that explicit, rather than rely on signed-shift undefined behavior.
    text=text.replace('nybbles <<= 4','nybbles = (int32_t)((uint32_t)nybbles << 4)')
    text=text.replace('s = (s << shift) >> 1;', 's = (int32_t)((uint32_t)s << shift) >> 1;')
    text=text.replace('s = (s >> 25) << 11;', 's = (s >> 25) * 2048;')
    text=text.replace('((~0x2F00 << 16) << i) < 0', '(((uint32_t)~0x2F00u << 16) << i) & 0x80000000u')
    text=text.replace('int16_t *fwd, *rev;', 'const int16_t *fwd, *rev;')
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True);output.write_text(text)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();stage(a.source,a.output)
