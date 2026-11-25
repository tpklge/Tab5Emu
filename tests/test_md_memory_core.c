// Own synthetic 68000 programs; no commercial ROM is used by this test.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "m68k.h"
#include "gwenesis_savestate.h"
unsigned char *ROM_DATA,*M68K_RAM;
uint32_t tab5_md_fast_rom_end=4096;
#include <assert.h>
int vdp_68k_irq_ack(int level){(void)level;return 0x18;}
unsigned int m68k_read_memory_8(unsigned int a){a&=0xffffff;if(a>=0xe00000)return FETCH8RAM(a);return a<0x400000?FETCH8ROM(a&0x1fffff):255;}
unsigned int m68k_read_memory_16(unsigned int a){return m68k_read_memory_8(a)*256+m68k_read_memory_8(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return m68k_read_memory_16(a)*65536u+m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if((a&0xffffff)>=0xe00000)WRITE8RAM(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){m68k_write_memory_8(a,v>>8);m68k_write_memory_8(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
SaveState*saveGwenesisStateOpenForWrite(const char*s){(void)s;return NULL;}
SaveState*saveGwenesisStateOpenForRead(const char*s){(void)s;return NULL;}
void saveGwenesisStateSet(SaveState*s,const char*k,int v){(void)s;(void)k;(void)v;}
int saveGwenesisStateGet(SaveState*s,const char*k){(void)s;(void)k;return 0;}
void saveGwenesisStateSetBuffer(SaveState*s,const char*k,const void*p,int n){(void)s;(void)k;(void)p;(void)n;}
void saveGwenesisStateGetBuffer(SaveState*s,const char*k,void*p,int n){(void)s;(void)k;memset(p,0,n);}

static unsigned cursor;
static void word(unsigned w){ROM_DATA[cursor++]=w&255;ROM_DATA[cursor++]=w>>8;}
static void longword(unsigned w){word(w>>16);word(w);}
static void run_case(unsigned write_address,unsigned read_address,unsigned value,unsigned code_address){
 memset(ROM_DATA,0,4096);memset(M68K_RAM,0,65536);memset(m68k,0,sizeof(*m68k));
 cursor=0;longword(0xfffff0);longword(code_address);cursor=0x100;
 word(0x23fc);longword(value);longword(write_address);
 word(0x2039);longword(read_address);word(0x4e72);word(0x2700);
 unsigned code_size=cursor-0x100;
 if(code_address>=0xe00000)memcpy(M68K_RAM+(code_address&65535),ROM_DATA+0x100,code_size);
 m68k_init();m68k_pulse_reset();m68k_run(10000);
 assert(m68k->stopped && m68k_get_reg(M68K_REG_D0)==value);
 assert(m68k_get_reg(M68K_REG_PC)==code_address+code_size);
}
int main(void){
 ROM_DATA=calloc(1,4096);M68K_RAM=calloc(1,65536);m68k=calloc(1,sizeof(*m68k));
 for(unsigned bank=0xe0;bank<=0xff;++bank){
  run_case((bank<<16)|0x2340,0xff2340,0x89abcdefu,0x100);
  run_case(0xff2340,(bank<<16)|0x2340,0x76543210u,0x100);
  run_case((bank<<16)|0x2340,0xff2340,0xa5a55a5au,(bank<<16)|0x1002);
 }
 run_case(0xfffffe,0xe0fffe,0x12345678u,0x100);
 assert(M68K_RAM[0xffff]==0x12 && M68K_RAM[0xfffe]==0x34 && M68K_RAM[1]==0x56 && M68K_RAM[0]==0x78);
 free(m68k);free(M68K_RAM);free(ROM_DATA);
 puts("Actual M68000: data and instruction fetch in all 32 RAM mirrors, big-endian longword lanes and 64 KiB wrap passed.");return 0;
}
