#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct tab5_n64 tab5_n64;
typedef struct {bool fast_cpu,audio_hle;unsigned raster_step;} tab5_n64_options;
typedef struct {
  void *context;
  bool (*rom_read)(void*,uint32_t,void*,size_t);
  void (*video)(void*,const uint16_t*,unsigned,unsigned);
  void (*audio)(void*,const int16_t*,size_t,unsigned);
  void (*poll)(void*);
} tab5_n64_callbacks;
typedef struct {
  uint64_t instructions,vi_frames,rsp_instructions,rdp_lists,rom_reads,exceptions,ai_buffers,si_commands;
  uint64_t hle_tasks,hle_triangles,hle_pixels;
  uint64_t run_us,rsp_lle_us,graphics_hle_us,audio_hle_us,audio_hle_tasks;
  uint32_t pc,cause,epc,vi_origin,vi_width,sp_status,mi_interrupts;
  uint32_t count,compare,status,vi_status,mi_mask;
  size_t core_bytes;
  char error[128];
} tab5_n64_stats;
enum { TAB5_N64_RDRAM=8*1024*1024, TAB5_N64_BATTERY=2048+32768+131072+32768 };
tab5_n64 *tab5_n64_create(uint32_t rom_bytes,const tab5_n64_callbacks*);
void tab5_n64_destroy(tab5_n64*);
bool tab5_n64_boot(tab5_n64*);
bool tab5_n64_set_options(tab5_n64*,const tab5_n64_options*);
bool tab5_n64_run(tab5_n64*,unsigned instructions);
void tab5_n64_input(tab5_n64*,uint16_t buttons,int8_t x,int8_t y);
void tab5_n64_get_stats(tab5_n64*,tab5_n64_stats*);
uint8_t *tab5_n64_battery(tab5_n64*);
// Host inspection uses the same bus as guest CPU and DMA.
uint32_t tab5_n64_read(tab5_n64*,uint32_t physical);
void tab5_n64_write(tab5_n64*,uint32_t physical,uint32_t value,uint32_t mask);
#ifdef __cplusplus
}
#endif
