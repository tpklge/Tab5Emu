// Original host runner; guest instructions, CPU and RCP use the firmware core.
#include "n64_console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *rom,*pcm;
static uint16_t frame[320*240];
static unsigned frames,audio_frames;
static uint32_t audio_checksum=2166136261u;
static tab5_n64 *running_machine;
static bool read_rom(void*ctx,uint32_t offset,void*out,size_t n){(void)ctx;return !fseek(rom,offset,SEEK_SET)&&fread(out,1,n,rom)==n;}
static void video(void*ctx,const uint16_t*p,unsigned w,unsigned h){(void)ctx;if(w==320&&h==240){memcpy(frame,p,sizeof(frame));++frames;}
  if(running_machine && getenv("TAB5_N64_DEBUG") && frames>=320 && frames<=420 && frames%10==0)
    fprintf(stderr,"VI frame %u status %08x origin %08x width %u hstart %08x vstart %08x xscale %08x yscale %08x\n",frames,tab5_n64_read(running_machine,0x04400000),tab5_n64_read(running_machine,0x04400004),tab5_n64_read(running_machine,0x04400008),tab5_n64_read(running_machine,0x04400024),tab5_n64_read(running_machine,0x04400028),tab5_n64_read(running_machine,0x04400030),tab5_n64_read(running_machine,0x04400034));
  if(running_machine && getenv("TAB5_N64_PLAY")){
    uint16_t buttons=((frames>=300 && frames<330)||(frames>=750 && frames<780))?0x1000:
      ((frames>=500 && frames<530)||(frames>=1000 && frames<1030)||(frames>=1250 && frames<1280))?0x8000:0;
    tab5_n64_input(running_machine,buttons,frames>1200?40:0,frames>1200?40:0);
  }
}
static void audio(void*ctx,const int16_t*p,size_t n,unsigned rate){
  (void)ctx;(void)rate;audio_frames+=n;
  for(size_t i=0;i<n*2;++i)audio_checksum=(audio_checksum^(uint16_t)p[i])*16777619u;
  if(pcm && fwrite(p,4,n,pcm)!=n){perror("private PCM capture");exit(8);}
}
int main(int argc,char**argv){
  if(argc<2)return 2;
  rom=fopen(argv[1],"rb");if(!rom)return 3;fseek(rom,0,SEEK_END);long bytes=ftell(rom);rewind(rom);
  tab5_n64_callbacks cb={.rom_read=read_rom,.video=video,.audio=audio};
  tab5_n64*m=tab5_n64_create(bytes,&cb);if(!m)return 4;
  tab5_n64_options options={.raster_step=1};
#ifdef TAB5_N64_FAST_CPU
  options.fast_cpu=true;
#endif
#ifdef TAB5_N64_AUDIO_HLE
  options.audio_hle=true;
#endif
  if(getenv("TAB5_N64_FAST_CPU"))options.fast_cpu=atoi(getenv("TAB5_N64_FAST_CPU"))!=0;
  if(getenv("TAB5_N64_AUDIO_HLE"))options.audio_hle=atoi(getenv("TAB5_N64_AUDIO_HLE"))!=0;
  if(getenv("TAB5_N64_RASTER_STEP"))options.raster_step=atoi(getenv("TAB5_N64_RASTER_STEP"));
  if(!tab5_n64_set_options(m,&options))return 7;
  running_machine=m;
  if(!tab5_n64_boot(m))return 5;
  if(getenv("TAB5_N64_PCM_OUT")){pcm=fopen(getenv("TAB5_N64_PCM_OUT"),"wb");if(!pcm)return 8;}
  unsigned budget=argc>2?strtoul(argv[2],NULL,10):2000000;
  unsigned chunk=getenv("TAB5_N64_CHUNK")?strtoul(getenv("TAB5_N64_CHUNK"),NULL,10):12000;
  if(!chunk)return 6;
  unsigned target_vi=getenv("TAB5_N64_TARGET_VI")?strtoul(getenv("TAB5_N64_TARGET_VI"),NULL,10):0;
  for(unsigned n=0;n<budget;n+=chunk){if(!tab5_n64_run(m,chunk) || (target_vi && frames>=target_vi))break;}
  tab5_n64_stats s;tab5_n64_get_stats(m,&s);unsigned nonzero=0;uint32_t checksum=2166136261;
  if(getenv("TAB5_N64_DEBUG")){
    fprintf(stderr,"Count %08x Compare %08x Status %08x VI %08x/%u origin %08x MI %08x/%08x\n",s.count,s.compare,s.status,s.vi_status,s.vi_width,s.vi_origin,s.mi_interrupts,s.mi_mask);
    for(int i=-4;i<16;++i)fprintf(stderr,"%08x: %08x\n",s.epc+4*i,tab5_n64_read(m,(s.epc+4*i)&0x1fffffff));
  }
  for(unsigned i=0;i<320*240;++i){nonzero+=frame[i]!=0;checksum=(checksum^frame[i])*16777619u;}
  printf("{\"run_us\":%llu,\"rsp_lle_us\":%llu,\"graphics_hle_us\":%llu,\"audio_hle_us\":%llu,\"audio_hle_tasks\":%llu,\"instructions\":%llu,\"vi_frames\":%llu,\"frames\":%u,\"nonzero_pixels\":%u,\"frame_checksum\":%u,\"rsp_instructions\":%llu,\"rdp_lists\":%llu,\"hle_tasks\":%llu,\"hle_triangles\":%llu,\"hle_pixels\":%llu,\"rom_reads\":%llu,\"audio_frames\":%u,\"exceptions\":%llu,\"pc\":\"%08x\",\"cause\":\"%08x\",\"epc\":\"%08x\",\"sp_status\":\"%08x\",\"core_bytes\":%zu,\"error\":\"%s\"}\n",
    (unsigned long long)s.run_us,(unsigned long long)s.rsp_lle_us,(unsigned long long)s.graphics_hle_us,(unsigned long long)s.audio_hle_us,(unsigned long long)s.audio_hle_tasks,(unsigned long long)s.instructions,(unsigned long long)s.vi_frames,frames,nonzero,checksum,(unsigned long long)s.rsp_instructions,
    (unsigned long long)s.rdp_lists,(unsigned long long)s.hle_tasks,(unsigned long long)s.hle_triangles,(unsigned long long)s.hle_pixels,(unsigned long long)s.rom_reads,audio_frames,(unsigned long long)s.exceptions,s.pc,s.cause,s.epc,s.sp_status,s.core_bytes,s.error);
  if(argc>3){FILE*f=fopen(argv[3],"wb");if(f){fprintf(f,"P6\n320 240\n255\n");for(unsigned i=0;i<320*240;++i){uint16_t p=frame[i];uint8_t rgb[3]={(p>>11)*255/31,((p>>5)&63)*255/63,(p&31)*255/31};fwrite(rgb,1,3,f);}fclose(f);}}
  fprintf(stderr,"PCM checksum %08x\n",audio_checksum);
  int status=s.error[0]?1:0;if(pcm && fclose(pcm))status=8;tab5_n64_destroy(m);fclose(rom);return status;
}
