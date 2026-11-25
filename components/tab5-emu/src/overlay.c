// The game cores' code overlay (overlay.ld, include/overlay.h): executing
// from flash, every core's code is linked at one PSRAM address and the one a
// game needs is copied there from its slot in flash when it launches.
#include "overlay.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "sdkconfig.h"

#if CONFIG_SPIRAM_XIP_FROM_PSRAM

bool tab5_overlay_load(const char *core) {
  (void)core;
  return true;
}

const char *tab5_overlay_loaded(void) { return ""; }
const char *tab5_overlay_error(void) { return ""; }

#else

#include "esp_cache.h"
#include "esp_log.h"
#include "esp_timer.h"
#if defined(TAB5_SD_CORES)
#include "psa/crypto.h"
#endif

static const char *TAG = "overlay";

typedef void (*ctor_fn)(void);

// linker symbols (overlay.ld): the shared PSRAM range, and per core its slot
// in flash, its code size (the symbol's value) and its constructor table
extern uint8_t _tab5_ovl_start[], _tab5_ovl_end[];
#define TAB5_OVL_SYMBOLS(name)                                                                                        \
  extern const uint8_t _tab5_ovl_##name##_lma[];                                                                       \
  extern const char _tab5_ovl_##name##_size[];                                                                         \
  extern const ctor_fn _tab5_ovl_ctors_##name##_start[], _tab5_ovl_ctors_##name##_end[];
TAB5_OVL_SYMBOLS(gba)
TAB5_OVL_SYMBOLS(snes)
TAB5_OVL_SYMBOLS(genesis)
TAB5_OVL_SYMBOLS(doom)
TAB5_OVL_SYMBOLS(nes)
TAB5_OVL_SYMBOLS(gbc)
TAB5_OVL_SYMBOLS(sms)
TAB5_OVL_SYMBOLS(msx)
TAB5_OVL_SYMBOLS(pico8)
TAB5_OVL_SYMBOLS(jk)
TAB5_OVL_SYMBOLS(darkforces)
TAB5_OVL_SYMBOLS(mgs)
TAB5_OVL_SYMBOLS(scummvm)
TAB5_OVL_SYMBOLS(n64)
TAB5_OVL_SYMBOLS(sm64)

typedef struct {
  const char *name;
  const uint8_t *lma;
  size_t size;
  const ctor_fn *ctors, *ctors_end;
} member_t;

#define TAB5_OVL_MEMBER(n)                                                                                            \
  {#n, _tab5_ovl_##n##_lma, (size_t)(uintptr_t)_tab5_ovl_##n##_size, _tab5_ovl_ctors_##n##_start,                     \
   _tab5_ovl_ctors_##n##_end}

static int s_loaded = -1;
static uint16_t s_constructed; // bit per member: its constructors have run

static int find(const char *core, member_t *out) {
  const member_t members[] = {
      TAB5_OVL_MEMBER(gba),   TAB5_OVL_MEMBER(snes),  TAB5_OVL_MEMBER(genesis), TAB5_OVL_MEMBER(doom),
      TAB5_OVL_MEMBER(nes),   TAB5_OVL_MEMBER(gbc),   TAB5_OVL_MEMBER(sms),     TAB5_OVL_MEMBER(msx),
      TAB5_OVL_MEMBER(pico8), TAB5_OVL_MEMBER(jk),    TAB5_OVL_MEMBER(darkforces), TAB5_OVL_MEMBER(mgs),
      TAB5_OVL_MEMBER(scummvm),
      TAB5_OVL_MEMBER(n64), TAB5_OVL_MEMBER(sm64),
  };
  _Static_assert(sizeof(members) / sizeof(members[0]) <= 8 * sizeof(s_constructed), "one bit per member");
  for (int i = 0; i < (int)(sizeof(members) / sizeof(members[0])); i++) {
    if (strcmp(core, members[i].name) == 0) {
      *out = members[i];
      return i;
    }
  }
  return -1;
}

static const char *s_loaded_name = "";
static const char *s_error = "";

const char *tab5_overlay_error(void) { return s_error; }

#if defined(TAB5_SD_CORES)
static bool read_sd_core(const member_t *m, uint8_t *destination) {
  // The firmware embeds the exact linked payload SHA, size and load address.
  // These are fixed-build overlays, never independently linked plugins.
  uint32_t bytes, address;
  memcpy(&bytes,m->lma+8,4); memcpy(&address,m->lma+12,4);
  if(memcmp(m->lma,"T5SDOVL1",8) || bytes!=m->size || address!=(uint32_t)(uintptr_t)destination) {
    s_error="Nucleo SD incompativel com este firmware.";return false;
  }
  char hex[65];
  for(unsigned j=0;j<32;++j)snprintf(hex+2*j,3,"%02x",m->lma[16+j]);
  char path[192];snprintf(path,sizeof(path),"/sdcard/Tab5-Emu/cores/%s-%s.bin",m->name,hex);
  FILE *file=fopen(path,"rb");
  if(!file){s_error="Nucleo ausente no SD. Reinstale os arquivos de nucleos desta versao.";return false;}
  psa_hash_operation_t hash=PSA_HASH_OPERATION_INIT;
  bool ok=psa_crypto_init()==PSA_SUCCESS && psa_hash_setup(&hash,PSA_ALG_SHA_256)==PSA_SUCCESS;
  size_t offset=0;
  while(ok && offset<m->size) {
    const size_t count=m->size-offset>4096?4096:m->size-offset;
    if(fread(destination+offset,1,count,file)!=count){ok=false;break;}
    ok=psa_hash_update(&hash,destination+offset,count)==PSA_SUCCESS;offset+=count;
  }
  uint8_t digest[32];size_t digest_bytes=0;
  if(ok)ok=fgetc(file)==EOF && !ferror(file) && psa_hash_finish(&hash,digest,sizeof(digest),&digest_bytes)==PSA_SUCCESS && digest_bytes==32 && !memcmp(digest,m->lma+16,32);
  psa_hash_abort(&hash);if(fclose(file))ok=false;
  if(!ok){memset(destination,0,m->size);s_error="Nucleo SD incompleto ou corrompido. Reinstale os arquivos desta versao.";}
  return ok;
}
#endif

bool tab5_overlay_load(const char *core) {
  s_error="";
  member_t m;
  const int i = find(core, &m);
  if (i < 0) {
    ESP_LOGE(TAG, "no core '%s' in the overlay", core);
    return false;
  }
  if (i == s_loaded) {
    return true;
  }
  const size_t capacity = (size_t)(_tab5_ovl_end - _tab5_ovl_start);
  if (m.size > capacity) { // the linker makes the range the largest member's size
    ESP_LOGE(TAG, "%s: %u bytes of code, more than the overlay's %u", core, (unsigned)m.size, (unsigned)capacity);
    return false;
  }
  const int64_t t0 = esp_timer_get_time();
  s_loaded = -1;
  s_loaded_name = "";
#if defined(TAB5_SD_CORES)
  if(!read_sd_core(&m,_tab5_ovl_start))return false;
#else
  memcpy(_tab5_ovl_start, m.lma, m.size);
#endif
  // the code was written through the data cache: write it back to PSRAM, and
  // drop every cached line of the whole range (both cores' instruction caches
  // included, which may still hold the previous core's code)
  esp_cache_msync(_tab5_ovl_start, capacity,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_INVALIDATE | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
  __asm__ volatile("fence.i" ::: "memory");
  s_loaded = i;
  s_loaded_name = m.name;
  const int64_t t1 = esp_timer_get_time();
  if (!(s_constructed & (1u << i))) {
    // the core's static constructors, once per boot (its data persists)
    s_constructed |= 1u << i;
    for (const ctor_fn *c = m.ctors; c < m.ctors_end; c++) {
      (*c)();
    }
  }
  ESP_LOGI(TAG, "%s: %u KB of code loaded in %lld ms (%u constructors)", core, (unsigned)(m.size / 1024),
           (t1 - t0) / 1000, (unsigned)(m.ctors_end - m.ctors));
  return true;
}

const char *tab5_overlay_loaded(void) { return s_loaded_name; }

#endif
