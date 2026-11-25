// Original geometry only: verifies the staged renderer without any game assets.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <PR/gbi.h>
#include "gfx_pc.h"
#include "gfx_soft.h"
unsigned tab5_sm64_render_width,tab5_sm64_render_height;
struct GfxDimensions gfx_current_dimensions;
bool configFiltering=false,configEnableFog=true;
void *rg_alloc(size_t n,int caps){(void)caps;void*p=calloc(1,n);assert(p);return p;}
void tab5_sm64_panic(const char*fmt,...){(void)fmt;abort();}
int64_t rg_system_timer(void){return 0;}
int main(int argc,char**argv){
 assert(argc==3);unsigned w=atoi(argv[1]),h=atoi(argv[2]);tab5_sm64_render_width=w;tab5_sm64_render_height=h;
 gfx_current_dimensions.width=w;gfx_current_dimensions.height=h;
 struct GfxRenderingAPI*a=&gfx_soft_api;a->init();a->start_frame();a->set_viewport(0,0,w,h);a->set_scissor(0,0,w,h);
 uint8_t blue[]={0,0,255,255};a->fill_rect(-32,-32,w+32,h+32,blue);
 for(unsigned i=0;i<w*h;++i)assert(gfx_output[i]==0xffff0000u);
 a->create_and_load_new_shader(0x200);a->set_depth_test(false);a->set_depth_mask(false);a->set_use_alpha(false);
 float vertices[]={-.8f,-.8f,0,1,255,0,0, .8f,-.8f,0,1,255,0,0, 0,.8f,0,1,255,0,0};
 a->draw_triangles(vertices,sizeof(vertices)/sizeof(float),1);
 unsigned red=0;for(unsigned i=0;i<w*h;++i)red+=(gfx_output[i]&0xffffff)==0x0000ff;assert(red>w*h/8 && red<w*h/2);
 uint8_t green[]={0,255,0,255};a->fill_rect(w-2,h-2,w+500,h+500,green);assert(gfx_output[w*h-1]==0xff00ff00u);
 a->end_frame();a->shutdown();free(gfx_output);free(gfx_overlay_output);
 printf("Native raster %ux%u: clipped fill, colored triangle and edge bounds passed\n",w,h);
}
