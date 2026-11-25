// SPDX-License-Identifier: GPL-2.0-or-later
// Portable console adapter for the pinned Mupen64Plus pure interpreter.
// The guest has real RDRAM, MMIO/DMA, interrupt/TLB and Joybus state.
#include "n64_console.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/pure_interp.h"
#include "device/r4300/interrupt.h"
#include "device/rcp/mi/mi_controller.h"
#include "device/rdram/rdram.h"
#include "api/callbacks.h"
#include "device/pif/cic.h"
#include "device/pif/n64_cic_nus_6105.h"
#include "n64video.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#define alloc_zero(n) heap_caps_calloc(1,(n),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)
#define alloc_hot(n) heap_caps_calloc(1,(n),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)
#else
#define alloc_zero(n) calloc(1,(n))
#define alloc_hot(n) calloc(1,(n))
#endif

void tab5_rsp_init(uint8_t*,uint8_t*,uint32_t*,uint32_t*,uint32_t*,uint32_t*,void(*)(void),void(*)(void));
int tab5_rsp_run(void);
#ifdef TAB5_N64_DAEDALUS
#include "tab5_daedalus.h"
#endif
static int run_rsp(void);
#ifdef TAB5_N64_FAST_CPU
void tab5_fast_run(struct r4300_core*);
#endif
#ifdef TAB5_N64_AUDIO_HLE
#include "n64_audio_hle.h"
#endif
#ifdef ESP_PLATFORM
#include "esp_timer.h"
static uint64_t perf_us(void){return esp_timer_get_time();}
#else
#include <time.h>
static uint64_t perf_us(void){struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);return (uint64_t)ts.tv_sec*1000000+ts.tv_nsec/1000;}
#endif
static void process_rdp(void);
static void check_rcp(void);
enum { ROM_CACHE=65536, VI_PERIOD=781250, EEPROM_SIZE=2048, SRAM_OFF=2048, FLASH_OFF=34816, PAK_OFF=165888 };
struct tab5_n64_hot {struct r4300_core cpu;uint32_t spmem[2048];};
struct tab5_n64 {
  struct tab5_n64_hot *hot;
  struct mi_controller mi;
  struct rdram ram;
  tab5_n64_callbacks cb;
  tab5_n64_stats stats;
  tab5_n64_options options;
  uint32_t *dram;
  uint32_t sp[8],sp_pc,dp[8],vi[14],ai[6],pi[13],ri[8],si[7],rdram_regs[10];
  uint8_t pif[64],battery[TAB5_N64_BATTERY];
  uint8_t cache[ROM_CACHE];
  uint32_t rom_bytes,cache_page;
  uint32_t rom_entry;
  bool boot_handoff;
  uint32_t observed_count;
  uint64_t clock,event_deadline[16];
  unsigned remaining,rsp_remaining;
  uint64_t vi_limit;
  uint16_t buttons;
  int8_t stick_x,stick_y;
  uint32_t flash_offset,flash_mode,flash_status_hi,flash_status_lo;
  uint8_t flash_latch[128];
  bool flash_erase_pending,flash_write_pending,pif_formatted,eeprom_large;
  uint16_t frame[320*240];
  int16_t audio[1024*2];
  bool booted,renderer_ready;
#ifdef TAB5_N64_AUDIO_HLE
  bool audio_hle_ready;
#endif
#ifdef TAB5_N64_DAEDALUS
  bool daedalus_ready;
#endif
  bool rsp_deferred;
};
static struct tab5_n64 *active;
static void fail(const char *why){
  if(active && !active->stats.error[0])snprintf(active->stats.error,sizeof(active->stats.error),"%s",why);
  if(active)active->hot->cpu.stop=1;
}
void DebugMessage(int level,const char *fmt,...){
  if(level!=M64MSG_ERROR)return;
  char text[128];va_list args;va_start(args,fmt);vsnprintf(text,sizeof(text),fmt,args);va_end(args);fail(text);
}
void msg_error(const char *fmt,...){(void)fmt;fail("RDP renderer error");}
void msg_warning(const char *fmt,...){(void)fmt;}
void msg_debug(const char *fmt,...){(void)fmt;}
void parallel_init(uint32_t workers,bool busy){(void)workers;(void)busy;}
void parallel_run(void task(uint32_t)){task(0);}
uint32_t parallel_num_workers(void){return 1;}
void parallel_close(void){}

int64_t *r4300_regs(struct r4300_core *r){return r->regs;}
int64_t *r4300_mult_hi(struct r4300_core *r){return &r->hi;}
int64_t *r4300_mult_lo(struct r4300_core *r){return &r->lo;}
unsigned int *r4300_llbit(struct r4300_core *r){return &r->llbit;}
uint32_t *r4300_pc(struct r4300_core *r){return &r->interp_PC.addr;}
struct precomp_instr **r4300_pc_struct(struct r4300_core *r){return &r->pc;}
int *r4300_stop(struct r4300_core *r){return &r->stop;}
uint32_t *r4300_cp0_regs(struct cp0 *c){return c->regs;}
uint64_t *r4300_cp0_latch(struct cp0 *c){return &c->latch;}
int *r4300_cp0_cycle_count(struct cp0 *c){return &c->cycle_count;}
unsigned int *r4300_cp0_next_interrupt(struct cp0 *c){return &c->next_interrupt;}
uint32_t *r4300_cp0_last_addr(struct cp0 *c){return &c->last_addr;}
unsigned int get_r4300_emumode(struct r4300_core *r){return r->emumode;}
void invalidate_r4300_cached_code(struct r4300_core*r,uint32_t a,size_t n){(void)r;(void)a;(void)n;}
void generic_jump_to(struct r4300_core *r,unsigned int a){r->interp_PC.addr=a;}
void cp0_update_count(struct r4300_core *r){
  uint32_t n=((r->interp_PC.addr-r->cp0.last_addr)>>2)*r->cp0.count_per_op;
  r->cp0.regs[CP0_COUNT_REG]+=n;r->cp0.cycle_count=(int32_t)((uint32_t)r->cp0.cycle_count+n);r->cp0.last_addr=r->interp_PC.addr;
}
static void clock_sync(struct cp0*c){
  if(active){active->clock+=(uint32_t)(c->regs[CP0_COUNT_REG]-active->observed_count);active->observed_count=c->regs[CP0_COUNT_REG];}
}
static void queue_update(struct cp0*c){
  c->next_interrupt=c->q.first?c->q.first->data.count:c->regs[CP0_COUNT_REG]+0x3fffffff;
  c->cycle_count=(int32_t)(c->regs[CP0_COUNT_REG]-c->next_interrupt);
}
void remove_event(struct interrupt_queue *q,int type){
  struct node **at=&q->first;
  while(*at){if((*at)->data.type==type){struct node*n=*at;*at=n->next;n->data.type=0;break;}at=&(*at)->next;}
}
void add_interrupt_event_count(struct cp0*c,int type,unsigned int count){
  clock_sync(c);
  remove_event(&c->q,type);struct node*n=NULL;
  for(unsigned i=0;i<16;++i)if(!c->q.pool.nodes[i].data.type){n=&c->q.pool.nodes[i];break;}
  if(!n){fail("interrupt queue full");return;}
  n->data.type=type;n->data.count=count;struct node**at=&c->q.first;
  uint64_t delta=(uint32_t)(count-c->regs[CP0_COUNT_REG]);
  if(type==COMPARE_INT && !delta)delta=UINT64_C(0x100000000);
  uint64_t deadline=active->clock+delta;active->event_deadline[n-c->q.pool.nodes]=deadline;
  while(*at && active->event_deadline[*at-c->q.pool.nodes]<=deadline)at=&(*at)->next;
  n->next=*at;*at=n;queue_update(c);
}
void add_interrupt_event(struct cp0*c,int type,unsigned int delay){add_interrupt_event_count(c,type,c->regs[CP0_COUNT_REG]+delay);}
void translate_event_queue(struct cp0*c,unsigned int base){
  clock_sync(c);
  uint32_t delta=base-c->regs[CP0_COUNT_REG];
  for(struct node*n=c->q.first;n;n=n->next)n->data.count+=delta;
  c->regs[CP0_COUNT_REG]=base;active->observed_count=base;queue_update(c);
}
void tab5_n64_set_compare(struct r4300_core*r,uint32_t value){
  cp0_update_count(r);clock_sync(&r->cp0);r->cp0.regs[CP0_COMPARE_REG]=value;
  r->cp0.regs[CP0_CAUSE_REG]&=~CP0_CAUSE_IP7;add_interrupt_event_count(&r->cp0,COMPARE_INT,value);
}
void exception_general(struct r4300_core*r){
#ifndef ESP_PLATFORM
  if(active && active->stats.exceptions<24 && getenv("TAB5_N64_DEBUG"))
    fprintf(stderr,"exception %llu pc=%08x cause=%08x status=%08x delay=%u\n",(unsigned long long)active->stats.exceptions,r->interp_PC.addr,r->cp0.regs[CP0_CAUSE_REG],r->cp0.regs[CP0_STATUS_REG],r->delay_slot);
#endif
  uint32_t*c=r->cp0.regs;cp0_update_count(r);
  if(!(c[CP0_STATUS_REG]&CP0_STATUS_EXL)){
    c[CP0_EPC_REG]=r->interp_PC.addr-(r->delay_slot?4:0);
    c[CP0_CAUSE_REG]=(c[CP0_CAUSE_REG]&~CP0_CAUSE_BD)|(r->delay_slot?CP0_CAUSE_BD:0);
  }
  c[CP0_STATUS_REG]|=CP0_STATUS_EXL;
  r->interp_PC.addr=(c[CP0_STATUS_REG]&0x400000)?0xbfc00380:0x80000180;
  r->cp0.last_addr=r->interp_PC.addr;
  if(r->delay_slot){r->skip_jump=r->interp_PC.addr;r->cp0.cycle_count=0;}
  if(active)++active->stats.exceptions;
}
int check_cop1_unusable(struct r4300_core*r){if(r->cp0.regs[CP0_STATUS_REG]&CP0_STATUS_CU1)return 0;r->cp0.regs[CP0_CAUSE_REG]=(r->cp0.regs[CP0_CAUSE_REG]&~0x3000007c)|CP0_CAUSE_EXCCODE_CPU|CP0_CAUSE_CE1;exception_general(r);return 1;}
int check_cop2_unusable(struct r4300_core*r){if(r->cp0.regs[CP0_STATUS_REG]&CP0_STATUS_CU2)return 0;r->cp0.regs[CP0_CAUSE_REG]=(r->cp0.regs[CP0_CAUSE_REG]&~0x3000007c)|CP0_CAUSE_EXCCODE_CPU|CP0_CAUSE_CE2;exception_general(r);return 1;}
void r4300_check_interrupt(struct r4300_core*r,uint32_t cause,int set){
  if(set)r->cp0.regs[CP0_CAUSE_REG]|=cause;else r->cp0.regs[CP0_CAUSE_REG]&=~cause;
  uint32_t status=r->cp0.regs[CP0_STATUS_REG];
  if((status&7)==1 && (status&r->cp0.regs[CP0_CAUSE_REG]&0xff00))r->cp0.cycle_count=0;
}
static void check_rcp(void){if(active)r4300_check_interrupt(&active->hot->cpu,CP0_CAUSE_IP2,active->mi.regs[2]&active->mi.regs[3]);}

static uint32_t *rom_word(struct tab5_n64*m,uint32_t offset){
  if(offset+4>m->rom_bytes)return NULL;
  uint32_t page=offset&~(ROM_CACHE-1);
  if(page!=m->cache_page){
    size_t n=m->rom_bytes-page;if(n>ROM_CACHE)n=ROM_CACHE;
    memset(m->cache,0xff,ROM_CACHE);
    if(!m->cb.rom_read(m->cb.context,page,m->cache,n)){fail("SD ROM read failed");return NULL;}
    // Native uint32 words, as expected by both Mupen and the RCP plugins.
    for(size_t i=0;i<n;i+=4){uint8_t*p=m->cache+i;uint32_t v=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];memcpy(p,&v,4);}
    m->cache_page=page;++m->stats.rom_reads;
  }
  return (uint32_t*)(m->cache+(offset&(ROM_CACHE-1)));
}
static uint8_t dram_byte(struct tab5_n64*m,uint32_t a){return ((uint8_t*)m->dram)[(a&(TAB5_N64_RDRAM-1))^3];}
static void dram_set(struct tab5_n64*m,uint32_t a,uint8_t v){((uint8_t*)m->dram)[(a&(TAB5_N64_RDRAM-1))^3]=v;}
static uint8_t pak_crc(const uint8_t*p){
  uint8_t crc=0;for(unsigned i=0;i<=32;++i)for(int b=7;b>=0;--b){unsigned carry=crc&0x80;crc<<=1;if(i<32)crc|=(p[i]>>b)&1;if(carry)crc^=0x85;}return crc;
}
static void joybus(struct tab5_n64*m){
  uint8_t flags=m->pif[63];
  if(flags&2){
    char challenge[30],reply[30];
    for(unsigned i=0;i<15;++i){challenge[2*i]=m->pif[0x30+i]>>4;challenge[2*i+1]=m->pif[0x30+i]&15;}
    n64_cic_nus_6105(challenge,reply,30);
    for(unsigned i=0;i<15;++i)m->pif[0x30+i]=(reply[2*i]<<4)|reply[2*i+1];
    m->pif[0x2e]=m->pif[0x2f]=m->pif[63]=0;m->pif_formatted=false;return;
  }
  if(flags&0x30){m->pif[63]=0x80;m->pif_formatted=false;return;}
  if(flags&1)m->pif_formatted=true;
  if(!m->pif_formatted){m->pif[63]&=~8u;return;}
  unsigned pos=0,channel=0;
  while(pos<63 && channel<6){
    uint8_t tx=m->pif[pos];if(tx==0xfe)break;if(tx==0xff||tx==0xfd){++pos;continue;}if(!tx){++channel;++pos;continue;}
    if(pos+2>=63)break;unsigned rx=m->pif[pos+1]&63;tx&=63;
    if(!tx || pos+2+tx+rx>63)break;
    m->pif[pos+1]&=63;
    uint8_t*cmd=m->pif+pos+2;uint8_t*out=cmd+tx;memset(out,0,rx);bool found=false;
    if(channel==0){
      if((cmd[0]==0 || cmd[0]==0xff)&&rx>=3){out[0]=5;out[1]=0;out[2]=1;found=true;}
      else if(cmd[0]==1 && rx>=4){out[0]=m->buttons>>8;out[1]=m->buttons;out[2]=m->stick_x;out[3]=m->stick_y;found=true;}
      else if((cmd[0]==2||cmd[0]==3)&&tx>=3){
        unsigned a=((unsigned)cmd[1]<<8|cmd[2])&0xffe0;
        if(cmd[0]==2 && rx>=33){if(a<32768)memcpy(out,m->battery+PAK_OFF+a,32);out[32]=pak_crc(out);found=true;}
        if(cmd[0]==3 && tx>=35 && rx>=1){if(a<32768)memcpy(m->battery+PAK_OFF+a,cmd+3,32);out[0]=pak_crc(cmd+3);found=true;}
      }
    }else if(channel==4){
      if((cmd[0]==0||cmd[0]==0xff)&&rx>=3){out[0]=0;out[1]=m->eeprom_large?0xc0:0x80;out[2]=0;found=true;}
      else if(cmd[0]==4 && tx>=2 && rx>=8){memcpy(out,m->battery+(unsigned)cmd[1]*8,8);found=true;}
      else if(cmd[0]==5 && tx>=10 && rx>=1){memcpy(m->battery+(unsigned)cmd[1]*8,cmd+2,8);found=true;}
    }
    if(!found)m->pif[pos+1]|=0x80;
    pos+=2+tx+rx;++channel;++m->stats.si_commands;
  }
  m->pif[63]=0;
}
static void si_dma(struct tab5_n64*m,bool to_pif){
  uint32_t a=m->si[0]&0xfffff8;
  if(a+64>TAB5_N64_RDRAM){fail("SI DMA outside RDRAM");return;}
  if(to_pif){for(unsigned i=0;i<64;++i)m->pif[i]=dram_byte(m,a+i);joybus(m);}
  else{if(m->pif_formatted)joybus(m);for(unsigned i=0;i<64;++i)dram_set(m,a+i,m->pif[i]);}
  m->si[6]=0x1000;m->mi.regs[2]|=2;check_rcp();
}
static void sp_dma(struct tab5_n64*m,uint32_t v,bool to_dram){
  unsigned length=((v&0xfff)|7)+1,count=((v>>12)&255)+1,skip=(v>>20)&0xfff;
  uint32_t sa=m->sp[0]&0x1ff8,da=m->sp[1]&0xfffff8;
  for(unsigned block=0;block<count;++block){
    if(da+length>TAB5_N64_RDRAM){fail("SP DMA outside RDRAM");return;}
    for(unsigned i=0;i<length;++i){uint32_t sp=(sa&0x1000)|((sa+i)&0xfff);uint8_t*p=(uint8_t*)m->hot->spmem;
      if(to_dram)dram_set(m,da+i,p[sp^3]);else p[sp^3]=dram_byte(m,da+i);}
    sa=(sa&0x1000)|((sa+length)&0xfff);da+=length+skip;
  }
  m->sp[0]=sa;m->sp[1]=da-skip;m->sp[2]=m->sp[3]=0xff8;m->sp[5]=m->sp[6]=0;
}
static void pi_dma(struct tab5_n64*m,uint32_t value,bool to_cart){
  uint32_t da=m->pi[0]&0xfffffe,ca=m->pi[1]&0xfffffffe;size_t n=(value&0xffffff)+1;
  if(da>=TAB5_N64_RDRAM || n>TAB5_N64_RDRAM-da){fail("PI DMA outside RDRAM");return;}
  if(ca>=0x10000000 && !to_cart){
    for(size_t i=0;i<n;++i){uint32_t off=ca-0x10000000+i;uint32_t*w=rom_word(m,off&~3u);dram_set(m,da+i,w?(uint8_t)(*w>>(24-8*(off&3))):255);}
  } else if(ca>=0x08000000 && ca<0x10000000){
    unsigned off=ca&0x1ffff;
    if(to_cart){for(size_t i=0;i<n;++i){uint8_t v=dram_byte(m,da+i);m->battery[SRAM_OFF+((off+i)&0x7fff)]=v;if(m->flash_mode==0xb4 && i<128)m->flash_latch[i]=v;}}
    else for(size_t i=0;i<n;++i){uint8_t v=m->battery[SRAM_OFF+((off+i)&0x7fff)];if(m->flash_mode==0xe1)v=(i<4)?(uint8_t)(m->flash_status_hi>>(24-8*i)):(i<8)?(uint8_t)(m->flash_status_lo>>(56-8*i)):0;else if(m->flash_mode==0xf0)v=m->battery[FLASH_OFF+((off*2+i)&0x1ffff)];dram_set(m,da+i,v);}
  }
  m->pi[0]=(da+n+7)&~7u;m->pi[1]=(ca+n+1)&~1u;m->pi[4]=8;m->mi.regs[2]|=0x10;check_rcp();
}
static void audio_dma(struct tab5_n64*m,uint32_t length){
  uint32_t address=m->ai[0]&0xfffff8;length&=0x3fff8;
  if(address+length>TAB5_N64_RDRAM){fail("AI DMA outside RDRAM");return;}
  unsigned rate=48681812u/((m->ai[4]&0x3fff)+1);
  if(rate<4000 || rate>96000)rate=32000;
  if(m->cb.audio && (m->ai[2]&1))for(uint32_t pos=0;pos<length;){
    unsigned count=(length-pos)/4;if(count>1024)count=1024;
    for(unsigned i=0;i<count*2;++i)m->audio[i]=(int16_t)((unsigned)dram_byte(m,address+pos+2*i)<<8|dram_byte(m,address+pos+2*i+1));
    m->cb.audio(m->cb.context,m->audio,count,rate);pos+=count*4;
  }
  m->ai[1]=m->ai[3]=0;++m->stats.ai_buffers;m->mi.regs[2]|=4;check_rcp();
}
static void video_frame(struct tab5_n64*m){
  ++m->stats.vi_frames;if(!m->cb.video)return;
  // Native VI origin/stride and RGBA5551/RGBA8888. Output is bounded to
  // 320x240 RGB565 for the Tab5's existing display pipeline.
  unsigned width=m->vi[2]&0xfff,kind=m->vi[0]&3;
  const unsigned hstart=(m->vi[9]>>16)&0x3ff,hend=m->vi[9]&0x3ff;
  const unsigned vstart=(m->vi[10]>>16)&0x3ff,vend=m->vi[10]&0x3ff;
  // libultra blanks H_START while reusing the framebuffer as temporary RAM.
  // Scanout must output black during that interval, never display that RAM.
  if(kind<2 || hend<=hstart || vend<=vstart){
    memset(m->frame,0,sizeof(m->frame));m->cb.video(m->cb.context,m->frame,320,240);return;
  }
  if(!width || width>2048)return;
  uint32_t origin=m->vi[1]&0xffffff;unsigned height=240;
  unsigned vspan=((m->vi[10]&0x3ff)-((m->vi[10]>>16)&0x3ff))&0x3ff;
  unsigned ys=m->vi[13]&0xfff;if(vspan && ys)height=(vspan*ys)>>11;
  if(!height || height>1024)return;
  unsigned bytes=kind==3?4:2;
  if((uint64_t)origin+(uint64_t)width*height*bytes>TAB5_N64_RDRAM)return;
  for(unsigned y=0;y<240;++y)for(unsigned x=0;x<320;++x){
    uint32_t a=origin+((y*height/240)*width+x*width/320)*bytes;uint16_t p;
    if(bytes==2){unsigned v=(unsigned)dram_byte(m,a)<<8|dram_byte(m,a+1);p=(v&0xffc0)|((v&0x40)>>1)|((v&0x3e)>>1);}
    else{p=((dram_byte(m,a)&0xf8)<<8)|((dram_byte(m,a+1)&0xfc)<<3)|(dram_byte(m,a+2)>>3);}
    m->frame[y*320+x]=p;
  }
  m->cb.video(m->cb.context,m->frame,320,240);
}
void gen_interrupt(struct r4300_core*r){
  struct tab5_n64*m=active;if(!m)return;
  clock_sync(&r->cp0);
  if(r->skip_jump){r->interp_PC.addr=r->skip_jump;r->skip_jump=0;r->cp0.last_addr=r->interp_PC.addr;}
  unsigned guard=0;
  while(r->cp0.q.first && m->clock>=m->event_deadline[r->cp0.q.first-r->cp0.q.pool.nodes] && guard++<32){
    int type=r->cp0.q.first->data.type;uint32_t count=r->cp0.q.first->data.count;remove_event(&r->cp0.q,type);
    if(type==VI_INT){m->mi.regs[2]|=8;video_frame(m);add_interrupt_event_count(&r->cp0,VI_INT,count+VI_PERIOD);}
    if(type==COMPARE_INT){r->cp0.regs[CP0_CAUSE_REG]|=CP0_CAUSE_IP7;add_interrupt_event_count(&r->cp0,COMPARE_INT,count);}
  }
  queue_update(&r->cp0);check_rcp();uint32_t status=r->cp0.regs[CP0_STATUS_REG];
  if((status&7)==1 && (status&r->cp0.regs[CP0_CAUSE_REG]&0xff00)){
    r->cp0.regs[CP0_CAUSE_REG]&=~CP0_CAUSE_EXCCODE_MASK;exception_general(r);
  }
}
void tlb_map(struct tlb*t,size_t i){(void)t;(void)i;}
void tlb_unmap(struct tlb*t,size_t i){(void)t;(void)i;}
static bool translate(struct r4300_core*r,uint32_t a,bool write,bool fetch,uint32_t*out){
  if((a&0xc0000000)==0x80000000){*out=a&0x1fffffff;return true;}
  for(unsigned i=0;i<32;++i){struct tlb_entry*e=&r->cp0.tlb.entries[i];
    uint32_t mask=((uint32_t)e->mask<<13)|0x1fff;
    if((a&~mask)!=(e->vpn2<<13&~mask) || (!e->g && e->asid!=(r->cp0.regs[CP0_ENTRYHI_REG]&255)))continue;
    bool odd=a&((mask+1)>>1),valid=odd?e->v_odd:e->v_even,dirty=odd?e->d_odd:e->d_even;
    if(valid && (!write||dirty)){uint32_t page_mask=mask>>1;*out=((odd?e->pfn_odd:e->pfn_even)<<12&~page_mask)|(a&page_mask);return true;}
    if(valid&&write&&!dirty){r->cp0.regs[CP0_CAUSE_REG]=(r->cp0.regs[CP0_CAUSE_REG]&~0x7c)|4;goto miss;}
    break;
  }
  r->cp0.regs[CP0_CAUSE_REG]=(r->cp0.regs[CP0_CAUSE_REG]&~0x7c)|(write?12:8);
miss:
  r->cp0.regs[CP0_BADVADDR_REG]=a;r->cp0.regs[CP0_CONTEXT_REG]=(r->cp0.regs[CP0_CONTEXT_REG]&0xff800000)|((a>>9)&0x7ffff0);
  r->cp0.regs[CP0_ENTRYHI_REG]=(r->cp0.regs[CP0_ENTRYHI_REG]&255)|(a&0xffffe000);
  if(!fetch)r->interp_PC.addr-=4;
  exception_general(r);return false;
}
uint32_t *fast_mem_access(struct r4300_core*r,uint32_t address){
  uint32_t a;if(!translate(r,address,false,true,&a))return NULL;
  if(a<TAB5_N64_RDRAM)return &active->dram[(a&~3u)/4];
  if(a>=0x04000000&&a<0x04002000)return &active->hot->spmem[(a&0x1fff)/4];
  if(a>=0x10000000&&a<0x1fc00000){uint32_t*p=rom_word(active,(a-0x10000000)&~3u);if(p)return p;}
  fail("instruction fetch outside executable memory");return NULL;
}
uint32_t tab5_n64_peek_opcode(struct r4300_core*r,uint32_t a){uint32_t*p=fast_mem_access(r,a);return p?*p:0;}
int r4300_read_aligned_word(struct r4300_core*r,uint32_t address,uint32_t*value){uint32_t a;if(!translate(r,address,false,false,&a))return 0;*value=tab5_n64_read(active,a&~3u);return 1;}
int r4300_write_aligned_word(struct r4300_core*r,uint32_t address,uint32_t value,uint32_t mask){uint32_t a;if(!translate(r,address,true,false,&a))return 0;tab5_n64_write(active,a&~3u,value,mask);return 1;}
int r4300_read_aligned_dword(struct r4300_core*r,uint32_t a,uint64_t*v){uint32_t hi,lo;if(!r4300_read_aligned_word(r,a,&hi)||!r4300_read_aligned_word(r,a+4,&lo))return 0;*v=(uint64_t)hi<<32|lo;return 1;}
int r4300_write_aligned_dword(struct r4300_core*r,uint32_t a,uint64_t v,uint64_t mask){return r4300_write_aligned_word(r,a,v>>32,mask>>32)&&r4300_write_aligned_word(r,a+4,v,mask);}
int tab5_n64_trace(struct r4300_core*r){
  if(active->stats.error[0]){r->stop=1;return 0;}
  if((!active->remaining || active->stats.vi_frames>=active->vi_limit) && !r->delay_slot){r->stop=1;return 0;}
  if(!active->boot_handoff && (r->interp_PC.addr&0x1fffffff)==(active->rom_entry&0x1fffffff)){
    // IPL3 has transferred control to the cart. Expose the installed 8 MiB
    // through the two boot-information layouts (x105 uses 0x3f0). The
    // compact RDRAM register model does not simulate electrical chip probing.
    active->dram[0x318/4]=TAB5_N64_RDRAM;active->dram[0x3f0/4]=TAB5_N64_RDRAM;
    active->boot_handoff=true;
  }
  if(active->remaining)--active->remaining;++active->stats.instructions;r->regs[0]=0;return 1;
}
int tab5_n64_rsp_poll(void){
  if(!active || !active->rsp_remaining--){fail("RSP instruction budget exceeded");return 0;}
  if((++active->stats.rsp_instructions&4095)==0 && active->cb.poll)active->cb.poll(active->cb.context);
  return 1;
}
static void process_rdp(void){if(active && active->renderer_ready){++active->stats.rdp_lists;n64video_process_list();}}

uint32_t tab5_n64_read(tab5_n64*m,uint32_t a){
  active=m;
  if(a<TAB5_N64_RDRAM)return m->dram[(a&~3u)/4];
  if(a>=0x04000000&&a<0x04002000)return m->hot->spmem[(a&0x1fff)/4];
  if(a>=0x10000000&&a<0x1fc00000){uint32_t*p=rom_word(m,(a-0x10000000)&~3u);return p?*p:0xffffffff;}
  if(a>=0x08000000&&a<0x10000000){if(m->flash_mode==0xe1)return (a&4)?m->flash_status_lo:m->flash_status_hi;unsigned off=a&0x7fff;uint8_t*p=m->battery+SRAM_OFF+off;return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
  if(a>=0x1fc007c0&&a<0x1fc00800){unsigned off=a&63;return (uint32_t)m->pif[off]<<24|(uint32_t)m->pif[off+1]<<16|(uint32_t)m->pif[off+2]<<8|m->pif[off+3];}
  unsigned i=(a&0xffff)/4;
  switch(a>>16){
  case 0x03f0:return m->rdram_regs[i%10];
  case 0x0404:if(i<8){uint32_t v=m->sp[i];if(i==7)m->sp[7]=1;return v;}break;
  case 0x0408:return m->sp_pc&0xffc;
  case 0x0410:if(i<8)return m->dp[i];break;
  case 0x0430:if(i<4)return m->mi.regs[i];break;
  case 0x0440:if(i<14){if(i==4){cp0_update_count(&m->hot->cpu);return ((m->hot->cpu.cp0.regs[CP0_COUNT_REG]%VI_PERIOD)*525/VI_PERIOD)&~1u;}return m->vi[i];}break;
  case 0x0450:if(i<6)return m->ai[i];break;
  case 0x0460:if(i<13)return m->pi[i];break;
  case 0x0470:if(i<8)return m->ri[i];break;
  case 0x0480:if(i<7)return m->si[i];break;
  default:break;
  }
  return (a&0xffff)|((a&0xffff)<<16);
}
void tab5_n64_write(tab5_n64*m,uint32_t a,uint32_t v,uint32_t mask){
  active=m;
  if(a<TAB5_N64_RDRAM){uint32_t*p=m->dram+(a&~3u)/4;*p=(*p&~mask)|(v&mask);return;}
  if(a>=0x04000000&&a<0x04002000){uint32_t*p=m->hot->spmem+(a&0x1fff)/4;*p=(*p&~mask)|(v&mask);return;}
  if(a>=0x08000000&&a<0x10000000){
    if(a==0x08010000){unsigned cmd=v>>24;m->flash_mode=cmd;
      if(cmd==0x4b||cmd==0xa5)m->flash_offset=(v&1023)*128;
      if(cmd==0x78){m->flash_status_hi=0x11118008;m->flash_status_lo=0x00c20000;m->flash_erase_pending=true;}
      if(cmd==0xa5){m->flash_status_hi=0x11118004;m->flash_status_lo=0x00c20000;}
      if(cmd==0xb4)m->flash_write_pending=true;
      if(cmd==0xe1){m->flash_status_hi=0x11118001;m->flash_status_lo=0x00c20000;}
      if(cmd==0xd2){
        if(m->flash_erase_pending)memset(m->battery+FLASH_OFF+m->flash_offset,255,128);
        if(m->flash_write_pending)memcpy(m->battery+FLASH_OFF+m->flash_offset,m->flash_latch,128);
        m->flash_erase_pending=m->flash_write_pending=false;
      }return;}
    for(unsigned i=0;i<4;++i)if(mask&(0xffu<<(24-8*i)))m->battery[SRAM_OFF+((a+i)&0x7fff)]=v>>(24-8*i);return;
  }
  if(a>=0x1fc007c0&&a<0x1fc00800){unsigned off=a&63;for(unsigned i=0;i<4;++i)if(mask&(0xffu<<(24-8*i)))m->pif[off+i]=v>>(24-8*i);if(off==60)joybus(m);return;}
  unsigned i=(a&0xffff)/4;uint32_t *p=NULL;
  switch(a>>16){
  case 0x03f0:p=&m->rdram_regs[i%10];break;
  case 0x0404:
    if(i>=8)return;
    if(i==2||i==3){m->sp[i]=v;sp_dma(m,v,i==3);return;}
    if(i==4){
      if(v&1)m->sp[4]&=~1u;if(v&2)m->sp[4]|=1;if(v&4)m->sp[4]&=~2u;
      if(v&8)m->mi.regs[2]&=~1u;if(v&16)m->mi.regs[2]|=1;
      for(unsigned b=0;b<10;++b){unsigned flag=b<2?b+5:b+5;if(v&(1u<<(5+2*b)))m->sp[4]&=~(1u<<flag);if(v&(1u<<(6+2*b)))m->sp[4]|=1u<<flag;}
      check_rcp();if(!(m->sp[4]&3)){m->rsp_remaining=2000000;m->rsp_deferred=run_rsp();check_rcp();}return;
    }
    if(i==7){m->sp[7]=0;return;}p=&m->sp[i];break;
  case 0x0408:m->sp_pc=v&0xffc;return;
  case 0x0410:
    if(i>=8)return;
    if(i==0){m->dp[0]=v&0xfffff8;m->dp[2]=m->dp[0];return;}
    if(i==1){m->dp[1]=v&0xfffff8;if(!(m->dp[3]&2))process_rdp();return;}
    if(i==3){for(unsigned b=0;b<3;++b){if(v&(1u<<(2*b)))m->dp[3]&=~(1u<<b);if(v&(1u<<(2*b+1)))m->dp[3]|=1u<<b;}if(!(m->dp[3]&2))process_rdp();return;}
    p=&m->dp[i];break;
  case 0x0430:
    if(i==0){m->mi.regs[0]=v&0x7f;if(v&0x800)m->mi.regs[2]&=~32u;check_rcp();return;}
    if(i==3){for(unsigned b=0;b<6;++b){if(v&(1u<<(2*b)))m->mi.regs[3]&=~(1u<<b);if(v&(1u<<(2*b+1)))m->mi.regs[3]|=1u<<b;}check_rcp();return;}return;
  case 0x0440:if(i<14){if(i==4){m->mi.regs[2]&=~8u;check_rcp();return;}p=&m->vi[i];}break;
  case 0x0450:if(i<6){if(i==1){audio_dma(m,v);return;}if(i==3){m->mi.regs[2]&=~4u;check_rcp();return;}p=&m->ai[i];}break;
  case 0x0460:if(i<13){if(i==2||i==3){m->pi[i]=v;pi_dma(m,v,i==2);return;}if(i==4){if(v&2)m->mi.regs[2]&=~16u;m->pi[4]=0;check_rcp();return;}p=&m->pi[i];}break;
  case 0x0470:if(i<8)p=&m->ri[i];break;
  case 0x0480:if(i<7){if(i==1||i==4){m->si[i]=v;si_dma(m,i==4);return;}if(i==6){m->si[6]=0;m->mi.regs[2]&=~2u;check_rcp();return;}p=&m->si[i];}break;
  default:break;
  }
  if(p)*p=(*p&~mask)|(v&mask);
}

static void rsp_done(struct tab5_n64*m){
  m->sp[4]|=0x203;if(m->sp[4]&0x40)m->mi.regs[2]|=1;check_rcp();
}
static int run_rsp(void){
#ifdef TAB5_N64_AUDIO_HLE
  if(active && active->audio_hle_ready && active->options.audio_hle){
    uint64_t start=perf_us();int result=tab5_n64_audio_hle_task();active->stats.audio_hle_us+=perf_us()-start;
    if(result<0){fail("N64 audio HLE: invalid command/span");return 0;}
    if(result>0){++active->stats.audio_hle_tasks;rsp_done(active);return 0;}
  }
#endif
#ifdef TAB5_N64_DAEDALUS
  struct tab5_n64*m=active;
  if(m && m->daedalus_ready){
    uint64_t start=perf_us();int result=tab5_daedalus_task();m->stats.graphics_hle_us+=perf_us()-start;
    if(result<0){fail("Daedalus: tarefa grafica invalida ou limite excedido");return 0;}
    if(result>0){
      m->sp[4]|=0x203; // SIG2/task done, BROKE, HALT.
      if(m->sp[4]&0x40)m->mi.regs[2]|=1;
      check_rcp();return 0;
    }
  }
#endif
  uint64_t start=perf_us();int result=tab5_rsp_run();if(active)active->stats.rsp_lle_us+=perf_us()-start;return result;
}

tab5_n64 *tab5_n64_create(uint32_t bytes,const tab5_n64_callbacks*cb){
  if(!cb || !cb->rom_read || bytes<4096 || bytes>64*1024*1024 || bytes%4)return NULL;
  struct tab5_n64*m=alloc_zero(sizeof(*m));if(!m)return NULL;
  // Small, frequently accessed CPU registers and SP memory stay internal;
  // large RDRAM/frame/cache/battery allocations remain in PSRAM.
  m->hot=alloc_hot(sizeof(*m->hot));if(!m->hot){free(m);return NULL;}
  m->dram=alloc_zero(TAB5_N64_RDRAM);if(!m->dram){free(m->hot);free(m);return NULL;}
  m->ram.dram=m->dram;m->ram.dram_size=TAB5_N64_RDRAM;m->hot->cpu.rdram=&m->ram;m->hot->cpu.mi=&m->mi;m->cb=*cb;m->rom_bytes=bytes;m->cache_page=UINT32_MAX;
#ifdef TAB5_N64_FAST_CPU
  m->options.fast_cpu=true;
#endif
#ifdef TAB5_N64_AUDIO_HLE
  m->options.audio_hle=true;
#endif
  m->options.raster_step=1;
  m->stats.core_bytes=sizeof(*m)+sizeof(*m->hot);memset(m->battery,255,sizeof(m->battery));return m;
}
bool tab5_n64_set_options(tab5_n64*m,const tab5_n64_options*o){
  if(!m || !o || m->booted || (o->raster_step!=1 && o->raster_step!=2))return false;
#ifndef TAB5_N64_FAST_CPU
  if(o->fast_cpu)return false;
#endif
#ifndef TAB5_N64_AUDIO_HLE
  if(o->audio_hle)return false;
#endif
  m->options=*o;return true;
}
bool tab5_n64_boot(tab5_n64*m){
  if(!m)return false;active=m;memset(&m->hot->cpu,0,sizeof(m->hot->cpu));memset(m->dram,0,TAB5_N64_RDRAM);memset(m->hot->spmem,0,sizeof(m->hot->spmem));
  memset(&m->mi,0,sizeof(m->mi));m->mi.regs[1]=0x02020102;
  m->hot->cpu.mi=&m->mi;m->hot->cpu.rdram=&m->ram;m->hot->cpu.emumode=EMUMODE_PURE_INTERPRETER;m->hot->cpu.pc=&m->hot->cpu.interp_PC;
  m->hot->cpu.cp0.count_per_op=2;m->hot->cpu.cp0.regs[CP0_STATUS_REG]=0x34000000;m->hot->cpu.cp0.regs[CP0_CONFIG_REG]=0x7006e463;
  m->hot->cpu.cp0.regs[CP0_PREVID_REG]=0xb22;m->hot->cpu.cp0.regs[CP0_RANDOM_REG]=31;
  poweron_cp1(&m->hot->cpu.cp1);poweron_cp2(&m->hot->cpu.cp2);
  for(unsigned i=0;i<1024;++i){uint32_t*p=rom_word(m,4*i);if(!p)return false;m->hot->spmem[i]=*p;}
  m->rom_entry=m->hot->spmem[2];m->boot_handoff=false;
  struct cic cic;init_cic_using_ipl3(&cic,m->hot->spmem+16);
  m->hot->cpu.regs[19]=0;m->hot->cpu.regs[20]=1;m->hot->cpu.regs[21]=0;m->hot->cpu.regs[22]=cic.seed;m->hot->cpu.regs[23]=0;
  m->pif[0x26]=cic.seed;
  const uint32_t imem_boot[]={0x3c0dbfc0,0x8da807fc,0x25ad07c0,0x31080080,0x5500fffc,0x3c0dbfc0,0x8da80024,0x3c0bb000};
  memcpy(m->hot->spmem+1024,imem_boot,sizeof(imem_boot));
  const unsigned code=m->hot->spmem[15]>>16;
  m->eeprom_large=code==0x444f || code==0x4d58 || code==0x4550; // DO/MX/EP, pinned ROM database.
  m->hot->cpu.regs[11]=(int64_t)(int32_t)0xa4000040;m->hot->cpu.regs[29]=(int64_t)(int32_t)0xa4001ff0;m->hot->cpu.regs[31]=(int64_t)(int32_t)0xa4001550;
  m->hot->cpu.interp_PC.addr=m->hot->cpu.cp0.last_addr=0xa4000040;m->sp[4]=1;m->dp[3]=0x80;m->vi[3]=1023;
  m->dram[0x318/4]=TAB5_N64_RDRAM;m->rdram_regs[0]=0xb4190010;m->rdram_regs[1]=0;m->rdram_regs[2]=0x2b3b1a0b;
  add_interrupt_event_count(&m->hot->cpu.cp0,VI_INT,VI_PERIOD);
  extern bool tab5_rdp_memory_acquire(void);
  if(!tab5_rdp_memory_acquire()){fail("PSRAM insuficiente para o RDP N64");return false;}
  m->renderer_ready=true;
  struct n64video_config config;n64video_config_init(&config);
  config.gfx.rdram=(uint8_t*)m->dram;config.gfx.rdram_size=TAB5_N64_RDRAM;config.gfx.dmem=(uint8_t*)m->hot->spmem;
  // The plugin stores the pointer array, so retain it in its own bridge.
  extern void tab5_rdp_init(struct n64video_config*,uint32_t*,uint32_t*);
  config.gfx.mi_intr_reg=&m->mi.regs[2];config.gfx.mi_intr_cb=check_rcp;config.parallel=false;config.num_workers=1;
  tab5_rdp_init(&config,m->vi,m->dp);m->renderer_ready=true;
  tab5_rsp_init((uint8_t*)m->dram,(uint8_t*)m->hot->spmem,m->sp,&m->sp_pc,m->dp,&m->mi.regs[2],check_rcp,process_rdp);
  #ifdef TAB5_N64_DAEDALUS
  if(!tab5_daedalus_init((uint8_t*)m->dram,(uint8_t*)m->hot->spmem,m->vi,m->mi.regs,m->cb.poll,m->cb.context)){
    fail("Nao foi possivel iniciar Daedalus HLE");return false;
  }
  m->daedalus_ready=true;tab5_daedalus_render_step(m->options.raster_step);
#endif
#ifdef TAB5_N64_AUDIO_HLE
  if(!tab5_n64_audio_hle_init((uint8_t*)m->dram,(uint8_t*)m->hot->spmem,m->cb.poll,m->cb.context)){fail("N64 audio HLE allocation failed");return false;}
  m->audio_hle_ready=true;
#endif
  m->booted=true;return !m->stats.error[0];
}
bool tab5_n64_run(tab5_n64*m,unsigned instructions){
  if(!m||!m->booted||m->stats.error[0])return false;active=m;uint64_t started=perf_us();m->remaining=instructions;m->vi_limit=m->stats.vi_frames+1;
  if(m->rsp_deferred && !(m->sp[4]&3)){m->rsp_remaining=2000000;m->rsp_deferred=run_rsp();check_rcp();}
#ifdef TAB5_N64_FAST_CPU
  if(m->options.fast_cpu)tab5_fast_run(&m->hot->cpu);else
#endif
  run_pure_interpreter(&m->hot->cpu);cp0_update_count(&m->hot->cpu);m->stats.run_us+=perf_us()-started;return !m->stats.error[0];
}
void tab5_n64_input(tab5_n64*m,uint16_t b,int8_t x,int8_t y){if(m){m->buttons=b;m->stick_x=x;m->stick_y=y;}}
uint8_t *tab5_n64_battery(tab5_n64*m){return m?m->battery:NULL;}
void tab5_n64_get_stats(tab5_n64*m,tab5_n64_stats*out){
  if(!m||!out)return;*out=m->stats;out->pc=m->hot->cpu.interp_PC.addr;out->cause=m->hot->cpu.cp0.regs[CP0_CAUSE_REG];out->epc=m->hot->cpu.cp0.regs[CP0_EPC_REG];
  out->vi_origin=m->vi[1];out->vi_width=m->vi[2];out->sp_status=m->sp[4];out->mi_interrupts=m->mi.regs[2];
  out->count=m->hot->cpu.cp0.regs[CP0_COUNT_REG];out->compare=m->hot->cpu.cp0.regs[CP0_COMPARE_REG];out->status=m->hot->cpu.cp0.regs[CP0_STATUS_REG];out->vi_status=m->vi[0];out->mi_mask=m->mi.regs[3];
#ifdef TAB5_N64_DAEDALUS
  if(m->daedalus_ready)tab5_daedalus_metrics(&out->hle_tasks,&out->hle_triangles,&out->hle_pixels);
#endif
}
void tab5_n64_destroy(tab5_n64*m){if(!m)return;
#ifdef TAB5_N64_DAEDALUS
if(m->daedalus_ready)tab5_daedalus_close();
#endif
#ifdef TAB5_N64_AUDIO_HLE
if(m->audio_hle_ready)tab5_n64_audio_hle_close();
#endif
if(m->renderer_ready)n64video_close();if(active==m)active=NULL;free(m->dram);free(m->hot);free(m);}

#ifdef TAB5_N64_FAST_CPU
// Share a translation unit so the fast interpreter can inline bus/register
// helpers. The separate reference interpreter remains available for A/B tests.
#undef RDRAM_MAX_SIZE
#define run_pure_interpreter tab5_fast_run
#include "device/r4300/pure_interp.c"
#undef run_pure_interpreter
#endif
