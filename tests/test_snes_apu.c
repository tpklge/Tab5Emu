#include "snes9x.h"
#include "apu_blargg.h"
#include "esp_heap_caps.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
SCPUState CPU;
SSettings Settings;
static int fail_internal,fail_external,allocations,last_caps;
static size_t internal_free=256*1024;
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return internal_free;}
void *heap_caps_calloc(size_t n,size_t size,unsigned caps) {
  if((caps&MALLOC_CAP_INTERNAL)?fail_internal:fail_external)return NULL;
  void *p=calloc(n,size);if(p){++allocations;last_caps=(int)caps;}return p;
}
void heap_caps_free(void *p){if(p)--allocations;free(p);}
static unsigned samples;
static void drain(void) {
  S9xFinalizeSamples();int count=S9xGetSampleCount();
  assert(count>=0 && count<32768 && !(count&1));
  int16_t output[32768];
  if(count){assert(S9xMixSamples(output,(unsigned)count));samples+=(unsigned)count;}
}
int main(void) {
  for(int fallback=0;fallback<3;++fallback){
    memset(&CPU,0,sizeof(CPU));memset(&Settings,0,sizeof(Settings));
    fail_internal=fallback==1;fail_external=0;internal_free=fallback==2?96*1024:256*1024;
    Settings.SoundPlaybackRate=Settings.SoundInputRate=32040;
    assert(S9xInitAPU());assert(allocations==1);
    assert((last_caps&(fallback?MALLOC_CAP_SPIRAM:MALLOC_CAP_INTERNAL))!=0);
    S9xSetSamplesAvailableCallback(drain);assert(S9xInitSound(0,0));S9xResetAPU();samples=0;
    for(int n=0;n<60;++n){for(int line=0;line<262;++line){CPU.Cycles+=1364;S9xAPUExecute();}drain();}
    assert(samples>60000 && samples<66000);
    uint8_t *state=calloc(1,SPC_SAVE_STATE_BLOCK_SIZE),*again=calloc(1,SPC_SAVE_STATE_BLOCK_SIZE);
    assert(state && again);S9xAPUSaveState(state);
    for(int n=0;n<5;++n){for(int line=0;line<262;++line){CPU.Cycles+=1364;S9xAPUExecute();}drain();}
    S9xAPULoadState(state);S9xAPUSaveState(again);assert(!memcmp(state,again,SPC_SAVE_STATE_BLOCK_SIZE));
    free(state);free(again);S9xDeinitAPU();S9xDeinitAPU();assert(!allocations);
  }
  fail_internal=fail_external=1;assert(!S9xInitAPU());S9xDeinitAPU();assert(!allocations);
  puts("SNES actual SPC/APU: one-second clock, state round trip, internal/fallback/OOM and lifetime OK");
}
