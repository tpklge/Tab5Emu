/* SPDX-License-Identifier: 0BSD
 * Headless runner for the separately licensed MIT mGBA suite assembly.
 * No proprietary BIOS/header/logo is included.
 */
typedef unsigned u32;
typedef unsigned char u8;
typedef int s32;
#define REG16(a) (*(volatile unsigned short *)(a))
#define SAVE ((volatile u8 *)0x0e000000)
struct Timings { s32 cycles[20]; };
#include "cases.h"
u32 activeTestInfo[2];
__attribute__((used)) const char signature[]="SRAM_V113";
extern u32 __iwram_start[],__iwram_end[],__iwram_load[];
extern u32 __ewram_start[],__ewram_end[],__ewram_load[];
static void put(unsigned offset,u32 value) {
  for(unsigned i=0;i<4;++i)SAVE[offset+i]=value>>(8*i);
}
void *memset(void *destination,int value,unsigned count) {
  volatile u8 *p=destination;
  for(unsigned i=0;i<count;++i)p[i]=(u8)value;
  return destination;
}
/* Referenced by unused branch variants in the imported assembly. */
void longjump(void) {}
void CLoop(void) { volatile s32 value=0;for(s32 i=0;i<16;++i)value+=i; }
int main(void) {
  REG16(0x04000208)=0;
  for(u32 *p=__iwram_start,*q=__iwram_load;p<__iwram_end;) *p++=*q++;
  for(u32 *p=__ewram_start,*q=__ewram_load;p<__ewram_end;) *p++=*q++;
  REG16(0x04000000)=0x0403;
  SAVE[0]='R';SAVE[1]='U';SAVE[2]='N';SAVE[3]='?';
  put(4,1);put(8,sizeof(cases)/sizeof(cases[0]));put(12,20);put(16,0);
  for(unsigned test=0;test<sizeof(cases)/sizeof(cases[0]);++test) {
    struct Timings baseline={{0}},observed={{0}};
    calibrate(&baseline);cases[test](&observed);
    for(unsigned mode=0;mode<20;++mode) {
      unsigned offset=0x100+test*240+mode*4;
      put(offset,baseline.cycles[mode]);put(offset+80,observed.cycles[mode]);
      put(offset+160,test?observed.cycles[mode]-baseline.cycles[mode]:observed.cycles[mode]);
    }
    put(16,test+1);
  }
  SAVE[0]='T';SAVE[1]='5';SAVE[2]='T';SAVE[3]='M';
  REG16(0x04000204)=0;
  for(unsigned i=0;i<240*160;++i)((volatile unsigned short *)0x06000000)[i]=0x03e0;
  for(;;) {}
}
