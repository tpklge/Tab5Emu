// Original 0BSD display lists: no ROM, BIOS, or proprietary microcode required.
#include "HLEGraphics/BaseRenderer.h"
#include "tab5_daedalus.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
static std::vector<uint32_t> ram(8 * 1024 * 1024 / 4), sp(8192 / 4);
static uint32_t vi[14]{}, mi[4]{};
static unsigned polls;
static constexpr uint32_t list = 0x10000, framebuffer = 0x100000;
static void poll(void *) { ++polls; }
static void command(unsigned &pc, uint32_t a, uint32_t b) {
  ram[pc++] = a;
  ram[pc++] = b;
}
static uint16_t pixel(unsigned x, unsigned y) {
  uint32_t word = ram[(framebuffer + (y * 320 + x) * 2) / 4];
  return x & 1 ? word & 65535 : word >> 16;
}
static void task() {
  auto *t = sp.data() + 0xfc0 / 4;
  t[0] = 1;
  t[4] = 0x1000;
  t[6] = 0x2000;
  t[7] = 32;
  t[12] = list;
}
static unsigned setup() {
  unsigned pc = list / 4;
  command(pc, 0xff10013f, framebuffer);
  command(pc, 0xfe000000, 0x200000);
  command(pc, 0xed000000, (320 * 4) << 12 | 240 * 4);
  return pc;
}
int main() {
  vi[2] = 320;
  vi[9] = (108 << 16) | 748;
  vi[10] = (37 << 16) | 511;
  vi[12] = 512;
  vi[13] = 1024;
  assert(tab5_daedalus_init(reinterpret_cast<uint8_t *>(ram.data()),
                            reinterpret_cast<uint8_t *>(sp.data()), vi, mi,
                            poll, nullptr));
  task();
  unsigned pc = setup();
  command(pc, 0xba001402, 3 << 20);
  command(pc, 0xf7000000, 0xf801f801);
  command(pc, 0xf6000000 | (319 * 4) << 12 | 239 * 4, 0);
  command(pc, 0xe9000000, 0);
  command(pc, 0xb8000000, 0);
  assert(tab5_daedalus_task() == 1);
  assert(pixel(160, 120) == 0xf801 && pixel(319, 239) == 0xf801);
  assert(mi[2] & 32);
  // Scissor restricts writes to [20,40) x [30,50).
  std::fill(ram.begin() + framebuffer / 4,
            ram.begin() + (framebuffer + 320 * 240 * 2) / 4, 0);
  pc = setup();
  command(pc, 0xed000000 | (20 * 4) << 12 | 30 * 4, (40 * 4) << 12 | 50 * 4);
  command(pc, 0xba001402, 3 << 20);
  command(pc, 0xf7000000, 0x07c107c1);
  command(pc, 0xf6000000 | (319 * 4) << 12 | 239 * 4, 0);
  command(pc, 0xb8000000, 0);
  assert(tab5_daedalus_task() == 1);
  assert(pixel(20, 30) == 0x07c1 && pixel(19, 30) == 0 && pixel(40, 30) == 0 &&
         pixel(20, 50) == 0);
  // Identity transformed, red, untextured triangle via the real F3D decoder.
  std::fill(ram.begin() + framebuffer / 4,
            ram.begin() + (framebuffer + 320 * 240 * 2) / 4, 0);
  auto *v = reinterpret_cast<FiddledVtx *>(
      reinterpret_cast<uint8_t *>(ram.data()) + 0x3000);
  for (unsigned i = 0; i < 3; ++i) {
    v[i] = {};
    v[i].rgba_a = 255;
    v[i].rgba_r = 255;
  }
  v[0].x = -1;
  v[0].y = -1;
  v[1].x = 1;
  v[1].y = -1;
  v[2].x = 0;
  v[2].y = 1;
  pc = setup();
  command(pc, 0xba001402, 0);
  command(pc, 0xb7000000, 4);
  uint64_t mux = (8ull << 37) | (8ull << 24) | (31ull << 32) | (4ull << 6) |
                 (7ull << 21) | (7ull << 3) | (7ull << 18) | 4;
  command(pc, 0xfc000000 | (mux >> 32), uint32_t(mux));
  command(pc, 0x04200030, 0x3000);
  command(pc, 0xbf000000, 0x00000a14);
  command(pc, 0xb8000000, 0);
  assert(tab5_daedalus_task() == 1);
  assert(pixel(160, 120) == 0xf801 && pixel(0, 0) == 0);
  uint64_t tasks, tris, pixels;
  tab5_daedalus_metrics(&tasks, &tris, &pixels);
  assert(tasks == 3 && tris == 1 && pixels > 76000);
  // A 4x4 RGBA16 texture, copied by actual SetTile/LoadTile/TexRect commands.
  for (unsigned i = 0; i < 8; ++i)
    ram[0x4000 / 4 + i] = 0x003f003f;
  pc = setup();
  command(pc, 0xfd100003, 0x4000);
  command(pc, 0xf5100200, 0x00080200);
  command(pc, 0xf4000000, 0x0000c00c);
  command(pc, 0xf2000000, 0x0000c00c);
  command(pc, 0xba001402, 2 << 20);
  command(pc, 0xe4000000 | (13 * 4) << 12 | 23 * 4, (10 * 4) << 12 | 20 * 4);
  command(pc, 0xb4000000, 0);
  command(pc, 0xb3000000, (4 * 1024) << 16 | 1024);
  command(pc, 0xb8000000, 0);
  assert(tab5_daedalus_task() == 1);
  assert(pixel(10, 20) == 0x003f && pixel(13, 23) == 0x003f);
  // A cyclic DL must yield and fail instead of hanging the host/device.
  pc = list / 4;
  command(pc, 0x06010000, list);
  assert(tab5_daedalus_task() == -1);
  assert(polls > 0);
  // Nested lists and out-of-RDRAM/misaligned pointers fail before dereference.
  pc = list / 4;
  command(pc, 0x06000000, list);
  assert(tab5_daedalus_task() == -1);
  sp[0xfc0 / 4 + 12] = 8 * 1024 * 1024 - 4;
  assert(tab5_daedalus_task() == -1);
  sp[0xfc0 / 4 + 12] = list + 1;
  assert(tab5_daedalus_task() == -1);
  task();
  sp[0xfc0 / 4] = 2;
  assert(tab5_daedalus_task() == 0);
  tab5_daedalus_close();
  std::cout << "Daedalus: F3D fill, scissor, transformed triangle, RGBA16 "
               "texrect, DP interrupt, "
               "cyclic/nested DL bounds, non-graphics fallback passed.\n";
}
