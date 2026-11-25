/* SPDX-License-Identifier: 0BSD
 * ARM/Thumb RAM self-modification and deterministic ALU workload.
 * Independent test, not a retail ROM or a complete CPU conformance suite.
 */
typedef unsigned u32;typedef unsigned short u16;typedef unsigned char u8;
#define REG16(a) (*(volatile u16*)(a))
#define SAVE ((volatile u8*)0x0e000000)
#define VIDEO ((volatile u16*)0x06000000)
__attribute__((used)) const char signature[]="SRAM_V113";
static void put(unsigned off,u32 n){for(unsigned b=0;b<4;++b)SAVE[off+b]=n>>(8*b);}
static volatile u32 workload_count=4096;
static u32 mix(void){u32 n=0x13579bdf;for(u32 i=0;i<workload_count;++i){n=(n<<5)|(n>>27);n^=i*0x9e3779b9;n+=0x1020304;n*=1664525;n^=n>>13;}return n;}
int main(void){
 REG16(0x04000000)=0x0403;
 volatile u32 *arm=(volatile u32*)0x03000000;
 volatile u16 *thumb=(volatile u16*)0x03000100;
 u32 (*af)(void)=(u32(*)(void))0x03000000;
 u32 (*tf)(void)=(u32(*)(void))0x03000101;
 unsigned failures=0,frames=0;
 for(unsigned i=0;i<240*160;++i)VIDEO[i]=0;
 for(;;){
  while(REG16(0x04000006)>=160){}while(REG16(0x04000006)<160){}
  arm[0]=0xe3a00005;arm[1]=0xe12fff1e;
  if(af()!=5)failures|=1;arm[0]=0xe3a00009;if(af()!=9)failures|=2;
  thumb[0]=0x2005;thumb[1]=0x4770;if(tf()!=5)failures|=4;
  thumb[0]=0x2009;if(tf()!=9)failures|=8;
  const u32 checksum=mix();
  SAVE[0]='T';SAVE[1]='5';SAVE[2]='R';SAVE[3]='V';put(4,++frames);put(8,checksum);put(12,failures);
  for(unsigned y=40;y<120;++y)for(unsigned x=60;x<180;++x)VIDEO[y*240+x]=failures?0x001f:0x03e0;
  for(unsigned i=0;i<32;++i)VIDEO[10*240+10+i]=(checksum>>i)&1?0x7fff:0;
 }
}
