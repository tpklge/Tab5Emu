/* SPDX-License-Identifier: 0BSD
 * Tab5-Emu diagnostic homebrew: mode 3, EASD marker, button LEDs, SRAM counter
 * and square wave. Contains no Nintendo logo or proprietary BIOS; run with
 * gpSP's "game" boot mode. It is not a retail cartridge boot conformance test.
 */
typedef unsigned short u16;
typedef unsigned char u8;
#define REG16(address) (*(volatile u16 *)(address))
#define SCREEN ((volatile u16 *)0x06000000)
#define SAVE ((volatile u8 *)0x0e000000)
__attribute__((used)) const char save_signature[] = "SRAM_V113";
static void rectangle(int x, int y, int w, int h, u16 color) {
  for (int yy = y; yy < y+h; ++yy)
    for (int xx = x; xx < x+w; ++xx) SCREEN[yy*240+xx] = color;
}
static void digit(int x, int y, unsigned value, u16 color) {
  static const u8 segments[10] = {0x3f,6,0x5b,0x4f,0x66,0x6d,0x7d,7,0x7f,0x6f};
  u8 bits=segments[value % 10];
  rectangle(x,y,14,24,0);
  if(bits&1) rectangle(x+2,y,10,2,color);
  if(bits&2) rectangle(x+12,y+2,2,9,color);
  if(bits&4) rectangle(x+12,y+13,2,9,color);
  if(bits&8) rectangle(x+2,y+22,10,2,color);
  if(bits&16)rectangle(x,y+13,2,9,color);
  if(bits&32)rectangle(x,y+2,2,9,color);
  if(bits&64)rectangle(x+2,y+11,10,2,color);
}
int main(void) {
  REG16(0x04000000) = 0x0403; /* mode 3, BG2 */
  REG16(0x04000084) = 0x0080;
  REG16(0x04000080) = 0x1177;
  REG16(0x04000082) = 0x0002;
  REG16(0x04000060) = 0;
  REG16(0x04000062) = 0xf080;
  REG16(0x04000064) = 0x86d6; /* ~440 Hz */
  rectangle(0,0,240,160,0);
  rectangle(0,0,80,5,0x001f); rectangle(80,0,80,5,0x03e0); rectangle(160,0,80,5,0x7c00);
  unsigned saved = SAVE[0] == 0x54 ? SAVE[1] : 0;
  unsigned frame = 0, previous = 0;
  int x=120,y=110;
  for(;;) {
    while(REG16(0x04000006) >= 160) {}
    while(REG16(0x04000006) < 160) {}
    unsigned keys = (~REG16(0x04000130)) & 1023;
    rectangle(x,y,5,5,0);
    if(keys&64 && y>72) --y;
    if(keys&128 && y<150) ++y;
    if(keys&32 && x>4) --x;
    if(keys&16 && x<230) ++x;
    if((keys&8) && !(previous&8)) { saved=(saved+1)&255; SAVE[0]=0x54; SAVE[1]=saved; }
    rectangle(x,y,5,5,(keys&1)?0x03e0:((keys&2)?0x7c00:0x7fff));
    for(unsigned i=0;i<10;++i) rectangle(8+i*22,12,16,10,(keys&(1<<i))?0x03e0:0x0842);
    digit(8,34,frame/1000,0x7fff);digit(26,34,frame/100,0x7fff);
    digit(44,34,frame/10,0x7fff);digit(62,34,frame,0x7fff);
    digit(165,34,saved/100,0x03ff);digit(183,34,saved/10,0x03ff);digit(201,34,saved,0x03ff);
    previous=keys;++frame;
  }
}
