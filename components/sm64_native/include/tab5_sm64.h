#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct tab5_sm64_callbacks {
 void *context;
 void (*video)(void*,const uint16_t*,unsigned,unsigned);
 void (*audio)(void*,const int16_t*,size_t,unsigned);
 bool (*save)(void*,const void*,size_t);
};
struct tab5_sm64_stats {uint64_t ticks,rendered_frames,run_us,render_us,audio_us,save_writes,save_failures,allocated_bytes;uint32_t level;char error[160];};
bool tab5_sm64_init(const struct tab5_sm64_callbacks*,const uint8_t save[512]);
bool tab5_sm64_resolution(unsigned width,unsigned height);
bool tab5_sm64_tick(bool draw);
void tab5_sm64_input(uint16_t buttons,int8_t x,int8_t y);
void tab5_sm64_stats(struct tab5_sm64_stats*);
const uint8_t*tab5_sm64_battery(void);
int tab5_sm64_eeprom_read(unsigned,uint8_t*,int);
int tab5_sm64_eeprom_write(unsigned,const uint8_t*,int);
#ifdef __cplusplus
}
#endif
