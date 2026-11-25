#pragma once
#include <stdint.h>
// TH handshake state for a 6-button pad. Count rising edges until timeout.
// This is independent of host polling speed: age advances in guest scanlines.
typedef struct {
  uint8_t phase, age, extra, enabled;
} Tab5MdPad;
static inline void tab5_md_pad_restore(Tab5MdPad *pad, const Tab5MdPad *saved) {
  const uint8_t enabled=pad->enabled;
  *pad=*saved;
  // The connected controller is a boot preference, not guest state. A
  // changed controller has no meaningful in-flight handshake to restore.
  if(pad->enabled!=enabled){pad->phase=pad->age=0;pad->extra=15;}
  pad->enabled=enabled;
}
static inline void tab5_md_pad_line(Tab5MdPad *p) {
  if (p->phase && ++p->age >= 26) {
    p->phase = 0;
    p->age = 0;
  }
}
static inline void tab5_md_pad_th(Tab5MdPad *p, uint8_t old, uint8_t next) {
  if (p->enabled && !(old & 64) && (next & 64) && p->phase < 8) {
    p->phase += 2;
    p->age = 0;
  }
}
static inline uint8_t tab5_md_pad_read(const Tab5MdPad *p, uint8_t th,
                                       uint8_t buttons) {
  const uint8_t ab = (buttons >> 2) & 0x30;
  if (p->enabled) {
    if (p->phase == 4 && !th)
      return ab;
    if (p->phase == 6)
      return th ? (uint8_t)(64 | (buttons & 0x30) | (p->extra & 15))
                : (uint8_t)(ab | 15);
  }
  return th ? (uint8_t)(64 | (buttons & 63)) : (uint8_t)(ab | (buttons & 3));
}
