// Tab5/IDF glue for the GPL gpSP RV32 backend. No changes to the other SDKs.
#include "common.h"
#include "esp_heap_caps.h"
#include "esp_mmu_map.h"
#include "esp_cache.h"
#include "hal/mmu_types.h"
#include <stdlib.h>
#include <assert.h>
#include "smc_tags.h"

u8 *memory_map_read[8*1024];
u16 oam_ram[512], palette_ram[512], palette_ram_converted[512], io_registers[512];
u8 *ewram, *iwram, *vram;
u32 tmemld[11][16], tmemst[4][16], thnjal[15*16];
static void *allocation, *mapping;
static size_t mapping_bytes;

static void check_jit_heap(const char *stage) {
#ifdef TAB5_GBA_HEAP_CHECK
  if (!heap_caps_check_integrity_all(true)) {
    printf("gba: JIT heap corruption at %s\n",stage);
    abort();
  }
#else
  (void)stage;
#endif
}

// Generic gpSP C stores serve the interpreter and do not inspect JIT tags.
// The RV32 stubs must return SMC alerts so their epilogue flushes RAM code
// and looks up the next guest PC instead of continuing in stale native code.
static cpu_alert_type ram_alert(u32 address,unsigned width) {
  return tab5_rv32_ram_has_code(address,width,ewram+0x40000,iwram)
      ? CPU_ALERT_SMC : CPU_ALERT_NONE;
}
cpu_alert_type tab5_rv32_write8(u32 address,u8 value) {
  return write_memory8(address,value) | ram_alert(address,1);
}
cpu_alert_type tab5_rv32_write16(u32 address,u16 value) {
  address &= ~1u;
  return write_memory16(address,value) | ram_alert(address,2);
}
cpu_alert_type tab5_rv32_write32(u32 address,u32 value) {
  address &= ~3u;
  return write_memory32(address,value) | ram_alert(address,4);
}

void *map_jit_block(unsigned size) {
  assert(!allocation && !mapping);
  const size_t page=CONFIG_MMU_PAGE_SIZE;
  const size_t bytes=(size+page-1)&~(page-1);
  allocation=heap_caps_aligned_alloc(page,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!allocation)return NULL;
  // Transfer ownership from the allocator's data view before making an
  // executable alias of the same physical pages.
  ESP_ERROR_CHECK(esp_cache_msync(allocation,bytes,
      ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_INVALIDATE));
  esp_paddr_t physical=0;mmu_target_t target=0;
  if(esp_mmu_vaddr_to_paddr(allocation,&physical,&target)!=ESP_OK ||
     target!=MMU_TARGET_PSRAM0 ||
     esp_mmu_map(physical,bytes,target,MMU_MEM_CAP_EXEC,ESP_MMU_MMAP_FLAG_PADDR_SHARED,&mapping)!=ESP_OK) {
    free(allocation);allocation=NULL;mapping=NULL;return NULL;
  }
  mapping_bytes=bytes;
#ifdef TAB5_GBA_HEAP_CHECK
  printf("gba: JIT backing=%p executable=%p bytes=%u\n",allocation,mapping,(unsigned)bytes);
#endif
  check_jit_heap("after map");
  return mapping;
}
void unmap_jit_block(void *ptr,unsigned size) {
  extern void tab5_rv32_shutdown(void);
  assert(ptr==mapping && size<=mapping_bytes);
  // esp_mmu_unmap removes the mapping but does not drain dirty data-cache
  // lines. Flush/invalidate the alias before its physical pages can return
  // to malloc, then invalidate the allocator view before freeing it.
  if(mapping) {
    check_jit_heap("before executable writeback");
    ESP_ERROR_CHECK(esp_cache_msync(mapping,mapping_bytes,
        ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_INVALIDATE));
    check_jit_heap("after executable writeback");
    ESP_ERROR_CHECK(esp_cache_msync(mapping,mapping_bytes,
        ESP_CACHE_MSYNC_FLAG_DIR_M2C|ESP_CACHE_MSYNC_FLAG_TYPE_INST));
    check_jit_heap("after executable invalidation");
    ESP_ERROR_CHECK(esp_cache_msync(allocation,mapping_bytes,
        ESP_CACHE_MSYNC_FLAG_DIR_M2C));
    check_jit_heap("after backing invalidation");
    __asm__ volatile("fence.i" ::: "memory");
  }
  tab5_rv32_shutdown();
  check_jit_heap("after worklist free");
  if(mapping)ESP_ERROR_CHECK(esp_mmu_unmap(mapping));
  check_jit_heap("after unmap");
  mapping=NULL;mapping_bytes=0;free(allocation);allocation=NULL;
}
