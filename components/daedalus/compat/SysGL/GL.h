#pragma once
#include "Base/Types.h"
enum {GL_CLAMP,GL_REPEAT,GL_MIRRORED_REPEAT,GL_CLAMP_TO_EDGE,GL_FOG,GL_FOG_START,GL_FOG_END,GL_FOG_COLOR,GL_SMOOTH,GL_FLAT,GL_PROJECTION,GL_MODELVIEW,GL_DEPTH_TEST};
enum EGuTextureWrapMode {GU_CLAMP=GL_CLAMP,GU_REPEAT=GL_REPEAT};
enum EGuMatrixType {GU_PROJECTION=GL_PROJECTION};
struct ScePspFMatrix4 {float m[16];};
inline void sceGuSetMatrix(EGuMatrixType,const ScePspFMatrix4*){}
inline void glFogf(int,float){} inline void glFogfv(int,const float*){} inline void glShadeModel(int){} inline void glEnable(int){} inline void glDisable(int){}
inline void glViewport(int,int,int,int){}
void tab5_daedalus_scissor(int,int,int,int);
inline void glScissor(int x,int y,int w,int h){tab5_daedalus_scissor(x,y,w,h);}


enum {GL_TEXTURE_2D,GL_RGBA,GL_UNSIGNED_SHORT_5_5_5_1};
void tab5_daedalus_framebuffer(const void*,unsigned,unsigned);
inline void glTexImage2D(int,int,int,int w,int h,int,int,int,const void*p){tab5_daedalus_framebuffer(p,w,h);}
extern bool gFogEnabled;
