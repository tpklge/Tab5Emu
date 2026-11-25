// Tab5 native SM64 frontend. Engine sources and ROM-derived assets stay external.
#include "tab5_sm64.h"
#include "rg_system.h"
#include <stdarg.h>
#include <setjmp.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "sm64.h"
#include "buffers.h"
#include "game/memory.h"
#include "game/object_list_processor.h"
#include "game/main.h"
#include "game/area.h"
#include "game/save_file.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/gfx/gfx_soft.h"
#include "pc/gfx/gfx_window_manager_api.h"
#include "pc/audio/audio_api.h"
#include "pc/configfile.h"
static struct tab5_sm64_callbacks callbacks;
_Static_assert(sizeof(struct SaveBuffer)==512,"Native EEPROM layout must match save conversion");
static struct tab5_sm64_stats metrics;
static uint8_t battery[512];
static uint16_t frame[320*240];
static jmp_buf failure;
static bool booted,render,drew_frame;
static uint16_t buttons;
static int8_t stick_x,stick_y;
static unsigned audio_phase;
unsigned tab5_sm64_render_width=160,tab5_sm64_render_height=120;
#define render_width tab5_sm64_render_width
#define render_height tab5_sm64_render_height
static uint16_t source_x[320],source_y[240];
extern void audio_init(void);extern void sound_init(void);
extern void game_loop_one_iteration(void);extern void thread5_game_loop(void*);
extern void create_next_audio_buffer(s16*,u32);
extern void sm64_init_buffers(void);extern void sm64_init_objects(void);
extern void sm64_init_gfx_output_buffer(void);extern void sm64_init_framebuffers(void);
extern void sm64_init_zbuffer(void);extern void sm64_init_gfx_pc(void);
extern struct GfxRenderingAPI gfx_soft_api;
void tab5_sm64_panic(const char*format,...){va_list ap;va_start(ap,format);vsnprintf(metrics.error,sizeof(metrics.error),format,ap);va_end(ap);longjmp(failure,1);}
void*rg_alloc(size_t bytes,int kind){
 void*p=NULL;if(kind==MEM_FAST)p=heap_caps_calloc(1,bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 if(!p)p=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!p)tab5_sm64_panic("Memoria insuficiente (%u bytes)",(unsigned)bytes);
 metrics.allocated_bytes+=bytes;return p;
}
int64_t rg_system_timer(void){return esp_timer_get_time();}
void rg_storage_commit(void){}
char*rg_emu_get_path(int type,const char*name){(void)type;(void)name;tab5_sm64_panic("Arquivo nativo nao suportado");return NULL;}
int tab5_sm64_eeprom_read(unsigned address,uint8_t*out,int bytes){if(!out || bytes<0 || address>64 || address*8u>512u || (unsigned)bytes>512u-address*8u)return -1;memcpy(out,battery+address*8,bytes);return 0;}
int tab5_sm64_eeprom_write(unsigned address,const uint8_t*data,int bytes){
 if(!data || bytes<0 || address>64 || address*8u>512u || (unsigned)bytes>512u-address*8u)return -1;
 uint8_t next[512];memcpy(next,battery,512);memcpy(next+address*8,data,bytes);
 // Keep the latest engine state in RAM even if the SD write fails. The
 // frontend retries this complete buffer before permitting quit or reset.
 memcpy(battery,next,512);
 if(callbacks.save && !callbacks.save(callbacks.context,battery,512)){++metrics.save_failures;return -1;}
 ++metrics.save_writes;return 0;
}
s32 osContInit(OSMesgQueue*q,u8*bits,OSContStatus*s){(void)q;(void)s;*bits=1;return 0;}
s32 osContStartReadData(OSMesgQueue*q){(void)q;return 0;}
void osContGetReadData(OSContPad*pad){pad->button=buttons;pad->stick_x=stick_x;pad->stick_y=stick_y;pad->errnum=0;}
void tab5_sm64_input(uint16_t b,int8_t x,int8_t y){buttons=b;stick_x=x;stick_y=y;}
void send_display_list(struct SPTask*task){if(booted && render){uint64_t start=esp_timer_get_time();gfx_run((Gfx*)task->task.t.data_ptr);metrics.render_us+=esp_timer_get_time()-start;drew_frame=true;}}
static void wm_init(const char*name,bool fullscreen){(void)name;(void)fullscreen;}
static void wm_keys(bool(*down)(int),bool(*up)(int),void(*clear)(void)){(void)down;(void)up;(void)clear;}
static void wm_full_cb(void(*fn)(bool)){(void)fn;}
static void wm_full(bool full){(void)full;}
static void wm_loop(void(*fn)(void)){fn();}
static void wm_size(uint32_t*w,uint32_t*h){*w=render_width;*h=render_height;}
bool tab5_sm64_resolution(unsigned w,unsigned h){
 if(booted || !((w==160 && h==120)||(w==128 && h==96)||(w==80 && h==60)))return false;
 render_width=w;render_height=h;return true;
}
static void wm_noop(void){}
static bool wm_frame(void){return render;}
static double wm_time(void){return esp_timer_get_time()/1000000.;}
static struct GfxWindowManagerAPI window={wm_init,wm_keys,wm_full_cb,wm_full,wm_loop,wm_size,wm_noop,wm_frame,wm_noop,wm_noop,wm_time,wm_noop};
static bool audio_init_stub(void){return true;}
static int audio_buffered(void){return 0;}
static int audio_desired(void){return 544;}
static void audio_play(const uint8_t*p,size_t n){if(callbacks.audio)callbacks.audio(callbacks.context,(const int16_t*)p,n/4,16000);}
static struct AudioAPI audio_api_impl={audio_init_stub,audio_buffered,audio_desired,audio_play,wm_noop};
struct AudioAPI*audio_api=&audio_api_impl;
struct GfxWindowManagerAPI*wm_api=&window;
struct GfxRenderingAPI*rendering_api=&gfx_soft_api;
bool tab5_sm64_init(const struct tab5_sm64_callbacks*cb,const uint8_t*save){
 if(booted || !cb || !save)return false;callbacks=*cb;memset(&metrics,0,sizeof(metrics));memcpy(battery,save,512);
 for(unsigned x=0;x<320;++x)source_x[x]=x*render_width/320;
 for(unsigned y=0;y<240;++y)source_y[y]=y*render_height/240;
 if(setjmp(failure))return false;
 sm64_init_buffers();sm64_init_objects();sm64_init_gfx_output_buffer();sm64_init_framebuffers();sm64_init_zbuffer();sm64_init_gfx_pc();
 void*pool=rg_alloc(4*1024*1024,MEM_SLOW);memset(pool,0,4*1024*1024);main_pool_init(pool,(uint8_t*)pool+4*1024*1024);gEffectsMemoryPool=mem_pool_init(0x4000,MEMORY_POOL_LEFT);
 render=true;gfx_init(&window,&gfx_soft_api,"SM64 Tab5",false);audio_init();sound_init();thread5_game_loop(NULL);booted=true;return true;
}
static uint32_t blend(uint32_t src,uint32_t dst){unsigned a=src>>24,ia=255-a;unsigned r=((src&255)*a+(dst&255)*ia)/255,g=(((src>>8)&255)*a+((dst>>8)&255)*ia)/255,b=(((src>>16)&255)*a+((dst>>16)&255)*ia)/255;return r|(g<<8)|(b<<16);}
bool tab5_sm64_tick(bool draw){
 if(!booted || metrics.error[0])return false;if(setjmp(failure))return false;
 uint64_t start=esp_timer_get_time();render=draw;drew_frame=false;gfx_start_frame();game_loop_one_iteration();gfx_end_frame();++metrics.ticks;metrics.level=gCurrLevelNum;
 uint64_t audio_start=esp_timer_get_time();int16_t pcm[544*2];unsigned frames=0;
 for(unsigned i=0;i<2;++i){unsigned n=audio_phase==2?256:272;audio_phase=(audio_phase+1)%3;create_next_audio_buffer(pcm+frames*2,n);frames+=n;}
 if(callbacks.audio)callbacks.audio(callbacks.context,pcm,frames,16000);metrics.audio_us+=esp_timer_get_time()-audio_start;
 if(draw && drew_frame && gfx_output){
  for(unsigned y=0;y<240;++y){
   const uint32_t*row=gfx_output+source_y[y]*render_width;
   const uint32_t*overlay=gfx_overlay_active && gfx_overlay_output?gfx_overlay_output+y*320:NULL;
   for(unsigned x=0;x<320;++x){
    uint32_t p=row[source_x[x]];
    if(overlay){uint32_t top=overlay[x];unsigned a=top>>24;if(a==255)p=top;else if(a)p=blend(top,p);}
    frame[y*320+x]=((p&0xf8)<<8)|(((p>>8)&0xfc)<<3)|((p>>19)&31);
   }
  }
  if(callbacks.video)callbacks.video(callbacks.context,frame,320,240);++metrics.rendered_frames;
 }
 metrics.run_us+=esp_timer_get_time()-start;return true;
}
void tab5_sm64_stats(struct tab5_sm64_stats*out){if(out)*out=metrics;}
const uint8_t*tab5_sm64_battery(void){return battery;}
