#include "n64_console.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t cart[262144];
static uint16_t frame[320*240];
static unsigned frames,samples;
static bool read_rom(void*ctx,uint32_t off,void*out,size_t n){(void)ctx;if(off>sizeof(cart)||n>sizeof(cart)-off)return false;memcpy(out,cart+off,n);return true;}
static void video(void*ctx,const uint16_t*p,unsigned w,unsigned h){(void)ctx;assert(w==320&&h==240);memcpy(frame,p,sizeof(frame));++frames;}
static void audio(void*ctx,const int16_t*p,size_t n,unsigned rate){(void)ctx;assert(p&&n&&rate>=4000&&rate<=96000);samples+=n;}
static void run(tab5_n64*m,unsigned n){for(unsigned i=0;i<n;++i)assert(tab5_n64_run(m,12000));}
int main(int argc,char**argv){
  assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);assert(fread(cart,1,8192,f)==8192);fclose(f);
  for(unsigned i=8192;i<sizeof(cart);++i)cart[i]=(uint8_t)(i*13+(i>>8));
  tab5_n64_callbacks cb={.rom_read=read_rom,.video=video,.audio=audio};
  assert(!tab5_n64_create(4095,&cb));assert(!tab5_n64_create(8193,&cb));assert(!tab5_n64_create(8192,NULL));
  tab5_n64*m=tab5_n64_create(sizeof(cart),&cb);assert(m&&tab5_n64_boot(m));
  run(m,160);assert(frames>=3 && samples>0);
  assert(tab5_n64_battery(m)[0]==1 && !memcmp(tab5_n64_battery(m)+1,"N64",3));
  for(unsigned i=0;i<320*240;++i)assert(frame[i]==0x07e0);
  assert(tab5_n64_read(m,0x3f00)==0); // Compare=Count must wait for wrap.
  tab5_n64_input(m,0x8000,0,0);run(m,120);
  for(unsigned i=0;i<320*240;++i)assert(frame[i]==0xf800);
  // libultra-style blanking must hide reused framebuffer data and resume.
  tab5_n64_write(m,0x04400024,0,~0u);run(m,80);
  for(unsigned i=0;i<320*240;++i)assert(frame[i]==0);
  tab5_n64_write(m,0x04400024,0x006c02ec,~0u);run(m,80);
  for(unsigned i=0;i<320*240;++i)assert(frame[i]==0xf800);
  tab5_n64_write(m,0x04400000,0,~0u);run(m,80);
  for(unsigned i=0;i<320*240;++i)assert(frame[i]==0);
  tab5_n64_write(m,0x04400000,2,~0u);run(m,80);
  for(unsigned i=0;i<320*240;++i)assert(frame[i]==0xf800);
  for(uint32_t off=65532;off<sizeof(cart);off+=65536){
    uint32_t wanted=(uint32_t)cart[off]<<24|(uint32_t)cart[off+1]<<16|(uint32_t)cart[off+2]<<8|cart[off+3];
    assert(tab5_n64_read(m,0x10000000+off)==wanted);
  }
  // Actual CXD4 scalar instructions and BREAK/interrupt, using the same
  // IMEM/status bus the guest uses (no simulated RSP success callback).
  tab5_n64_write(m,0x04001000,0x24081234,~0u); // addiu t0,zero,1234
  tab5_n64_write(m,0x04001004,0xac080000,~0u); // sw t0,0(zero)
  tab5_n64_write(m,0x04001008,0x0000000d,~0u); // break
  tab5_n64_write(m,0x04080000,0,~0u);
  tab5_n64_write(m,0x04040010,0x101,~0u);
  assert(tab5_n64_read(m,0x04000000)==0x1234);
  assert((tab5_n64_read(m,0x04040010)&3)==3);
  assert(tab5_n64_read(m,0x04300008)&1);
  // A reused controller command must be sampled again on SI read DMA.
  tab5_n64_input(m,0x4000,17,-29);
  tab5_n64_write(m,0x04800000,0x3000,~0u);tab5_n64_write(m,0x04800004,0x1fc007c0,~0u);
  assert((tab5_n64_read(m,0x3000)&255)==0x40);
  assert((tab5_n64_read(m,0x3004)&0xffffff00)==0x0011e300);
  // FlashRAM's 128-byte latch is committed only by D2; then read and erase.
  for(unsigned i=0;i<32;++i)tab5_n64_write(m,0x6000+4*i,0x50515200+i,~0u);
  tab5_n64_write(m,0x08010000,0xb4000000,~0u);
  tab5_n64_write(m,0x04600000,0x6000,~0u);tab5_n64_write(m,0x04600004,0x08000000,~0u);tab5_n64_write(m,0x04600008,127,~0u);
  tab5_n64_write(m,0x08010000,0xa5000002,~0u);
  assert(tab5_n64_battery(m)[34816+256]==255);
  tab5_n64_write(m,0x08010000,0xd2000000,~0u);
  assert(tab5_n64_battery(m)[34816+256]==0x50);
  tab5_n64_write(m,0x08010000,0xe1000000,~0u);assert(tab5_n64_read(m,0x08000000)==0x11118001);
  tab5_n64_write(m,0x08010000,0xf0000000,~0u);
  tab5_n64_write(m,0x04600000,0x7000,~0u);tab5_n64_write(m,0x04600004,0x08000080,~0u);tab5_n64_write(m,0x0460000c,127,~0u);
  assert(tab5_n64_read(m,0x7000)==0x50515200);
  tab5_n64_write(m,0x08010000,0x4b000002,~0u);tab5_n64_write(m,0x08010000,0x78000000,~0u);tab5_n64_write(m,0x08010000,0xd2000000,~0u);
  assert(tab5_n64_battery(m)[34816+256]==255);
  // DMA: IMEM page wrap, two rows and RDRAM skip.
  for(unsigned i=0;i<20;++i)tab5_n64_write(m,0x4000+4*i,0x11223300+i,~0u);
  tab5_n64_write(m,0x04040000,0x1ff0,~0u);tab5_n64_write(m,0x04040004,0x4000,~0u);
  tab5_n64_write(m,0x04040008,(8u<<20)|(1u<<12)|31,~0u);
  assert(tab5_n64_read(m,0x04001ff0)==0x11223300);
  assert(tab5_n64_read(m,0x04001000)==0x11223304);
  assert(tab5_n64_read(m,0x04001010)==0x1122330a);
#ifdef TAB5_N64_DAEDALUS
  // A real OSTask through guest-visible SP MMIO must dispatch to Daedalus,
  // complete SP/DP, and write RDRAM without running the graphics RSP program.
  const uint32_t dl[]={0xff10013f,0x600000,0xfe000000,0x650000,
    0xed000000,(320*4)<<12|240*4,0xba001402,3<<20,
    0xf7000000,0xf801f801,0xf6000000|(319*4)<<12|239*4,0,
    0xe9000000,0,0xb8000000,0};
  for(unsigned i=0;i<sizeof(dl)/4;++i)tab5_n64_write(m,0x710000+i*4,dl[i],~0u);
  tab5_n64_write(m,0x04400030,512,~0u); // Valid 320x240 HLE viewport.
  tab5_n64_write(m,0x04400028,(37u<<16)|511u,~0u);
  uint32_t task[16]={0};task[0]=1;task[4]=0x700000;task[6]=0x701000;task[7]=32;task[12]=0x710000;
  for(unsigned i=0;i<16;++i)tab5_n64_write(m,0x04000fc0+i*4,task[i],~0u);
  tab5_n64_stats before,after;tab5_n64_get_stats(m,&before);
  tab5_n64_write(m,0x04300000,0x800,~0u);tab5_n64_write(m,0x04040010,8,~0u);
  tab5_n64_write(m,0x04040010,0x105,~0u);
  tab5_n64_get_stats(m,&after);
  if(after.error[0] || after.hle_pixels<320*240)fprintf(stderr,"Daedalus MMIO: tasks=%llu pixels=%llu error=%s\n",(unsigned long long)after.hle_tasks,(unsigned long long)after.hle_pixels,after.error);
  assert(after.hle_tasks==before.hle_tasks+1 && after.hle_pixels>=320*240);
  assert(after.rsp_instructions==before.rsp_instructions && after.rdp_lists==before.rdp_lists);
  assert((after.sp_status&0x203)==0x203 && (after.mi_interrupts&33)==33);
  assert(tab5_n64_read(m,0x600000)==0xf801f801 && !after.error[0]);
#endif
  tab5_n64_stats s;tab5_n64_get_stats(m,&s);
  assert(s.core_bytes<1024*1024 && s.rdp_lists>0 && s.rsp_instructions>=3 && !s.error[0]);
  uint8_t battery[TAB5_N64_BATTERY];memcpy(battery,tab5_n64_battery(m),sizeof(battery));tab5_n64_destroy(m);
  m=tab5_n64_create(sizeof(cart),&cb);assert(m);memcpy(tab5_n64_battery(m),battery,sizeof(battery));assert(tab5_n64_boot(m));
  run(m,100);assert(tab5_n64_battery(m)[0]==2);
  tab5_n64_write(m,0x04600000,0x7ffff0,~0u);tab5_n64_write(m,0x04600004,0x10000000,~0u);tab5_n64_write(m,0x0460000c,4095,~0u);
  tab5_n64_get_stats(m,&s);assert(strstr(s.error,"PI DMA outside"));assert(!tab5_n64_run(m,100));
  tab5_n64_destroy(m);
  puts("N64 actual Mupen/CXD4/RDP: IPL3, Count/Compare wrap, ROM paging, PI/SP/SI/AI DMA, reused Joybus reads, RGB565, VI blank/unblank, EEPROM second boot and FlashRAM program/read/erase passed.");
}
