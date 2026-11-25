#pragma once
#include <stdint.h>
// Pan Docs Audio_details.md: capacitor factors converted from 4194304 Hz to
// the port's exact 32768 Hz mixer rate. State uses Q16, with wide products.
typedef struct { int32_t capacitor[2]; } Tab5GbAudioFilter;
// NR50: left gain in bits 6..4, right in bits 2..0; zero still means gain 1.
static inline int tab5_gb_master_gain(uint8_t nr50,int channel) {
  return 1 + ((nr50 >> (channel == 0 ? 4 : 0)) & 7);
}
static inline int16_t tab5_gb_high_pass(Tab5GbAudioFilter *f,int channel,int sample,int enabled,int cgb) {
  if(!enabled)return 0;
  const int64_t input=(int64_t)sample*65536;
  const int64_t output=input-f->capacitor[channel];
  f->capacitor[channel]=(int32_t)(input-output*(cgb?57239:65185)/65536);
  const int value=(int)(output/65536);
  return value>32767?32767:value< -32768?-32768:(int16_t)value;
}
