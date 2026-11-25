// Local gnuboy allocation adapter; upstream sources remain pinned and intact.
#include "gb_memory.hpp"
#include "gbc_shared_memory.hpp"
#include "shared_memory.h"
extern "C" {
#include "gnuboy/defs.h"
#include "gnuboy/loader.h"
#include "gnuboy/lcd.h"
#include "gnuboy/cpu.h"
#include "gnuboy/mem.h"
#include "gnuboy/pcm.h"
extern byte *bgdup;
struct cpu *cpu = nullptr;
}
namespace {
uint8_t *vram_ptr, *wram_ptr, *audio_ptr;
bool owns_pool = false;
}

bool gb_memory_init() {
  // Do not clear another core's allocations, or initialize over a live GB.
  if (owns_pool || shared_mem_get_instance()) return false;
  owns_pool = true;
  vram_ptr = static_cast<uint8_t *>(shared_malloc(16 * 1024));
  wram_ptr = static_cast<uint8_t *>(shared_malloc(32 * 1024));
  scan = static_cast<gbc_scan *>(shared_malloc(sizeof(*scan)));
  gbc_filebuf = static_cast<uint8_t *>(shared_malloc(4096));
  cpu = static_cast<struct cpu *>(shared_malloc(sizeof(*cpu)));
  lcd = static_cast<struct lcd *>(shared_malloc(sizeof(*lcd)));
  audio_ptr = static_cast<uint8_t *>(shared_malloc(GBC_AUDIO_BUFFER_SIZE));
  bgdup = static_cast<byte *>(shared_malloc(256));
  if (!vram_ptr || !wram_ptr || !scan || !gbc_filebuf || !cpu || !lcd || !audio_ptr || !bgdup) {
    gbc_free_shared_memory();
    return false;
  }
  ram.loaded = 0;
  return true;
}
extern "C" void gbc_free_shared_memory() {
  if (!owns_pool) return;
  shared_mem_clear();
  owns_pool = false;
  vram_ptr = wram_ptr = audio_ptr = nullptr;
  scan = nullptr; cpu = nullptr; lcd = nullptr;
  gbc_filebuf = bgdup = nullptr;
  ram.ibank = nullptr;
  pcm.buf = nullptr; pcm.len = pcm.pos = 0;
}
extern "C" void gbc_get_memory_regions(uint8_t **vram, uint8_t **wram, uint8_t **audio) {
  if (vram) *vram = vram_ptr;
  if (wram) *wram = wram_ptr;
  if (audio) *audio = audio_ptr;
}
