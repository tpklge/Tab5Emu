// SPDX-License-Identifier: 0BSD
#include "n64_audio_hle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t*ram,*sp;
static unsigned pc;
static void cmd(uint32_t a,uint32_t b){ram[pc++]=a;ram[pc++]=b;}
static void task(void){
 uint32_t*t=sp+0xfc0/4;memset(t,0,64);t[0]=2;t[6]=0x2000;t[12]=0x3000;
 ram[0x2000/4]=1;ram[(0x2000+0x30)/4]=0xf0000f00;ram[(0x2000+0x28)/4]=0x1e24138c;pc=0x3000/4;
}
static int run(void){sp[0xff4/4]=(pc-0x3000/4)*4;return tab5_n64_audio_hle_task();}
int main(void){
 ram=calloc(1,8*1024*1024);sp=calloc(1,8192);assert(ram&&sp);
 assert(tab5_n64_audio_hle_init((uint8_t*)ram,(uint8_t*)sp,NULL,NULL));
 for(unsigned i=0;i<4;++i)ram[0x4000/4+i]=0x12345600+i;
 task();cmd(0x08000000,0x01000010);cmd(0x04000000,0x4000);cmd(0x0a000000,0x01000010);cmd(0x06000000,0x5000);
 assert(run()==1 && !memcmp((uint8_t*)ram+0x4000,(uint8_t*)ram+0x5000,16));
 task();cmd(0x08000000,0x01000010);cmd(0x04000000,8*1024*1024-8);assert(run()==-1);
 task();cmd(0x0800f000,0x01000010);cmd(0x04000000,0x4000);assert(run()==-1);
 task();cmd(0x0b000200,0x4000);assert(run()==-1); // Oversized ADPCM table.
 task();cmd(0x0b000020,0x4001);assert(run()==-1); // Misaligned coefficients.
 ram[0x4000/4]=0x0f000000; // Predictor 15 is outside the eight-entry codebook.
 task();cmd(0x08000000,0x01000020);cmd(0x04000000,0x4000);cmd(0x01010000,0x6000);assert(run()==-1);
 task();sp[0xff4/4]=7;assert(tab5_n64_audio_hle_task()==-1);
 task();ram[(0x2000+0x28)/4]=0x12345678;assert(tab5_n64_audio_hle_task()==0);
 task();sp[0xfc0/4]=1;assert(tab5_n64_audio_hle_task()==0);
 tab5_n64_audio_hle_close();free(ram);free(sp);
 puts("N64 ABI1 HLE: load/move/save PCM preserved, RDRAM/buffer/table/list bounds refused, unknown microcode fallback passed.");
}
