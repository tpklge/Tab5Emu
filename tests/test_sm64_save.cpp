#include <array>
#include <span>
#include "sm64_save.hpp"
#include <cassert>
#include <cstdio>
static void signature(std::array<uint8_t,512>&b,unsigned off,unsigned n,unsigned magic){
 b[off+n-4]=magic>>8;b[off+n-3]=magic;unsigned sum=0;for(unsigned i=0;i<n-2;++i)sum+=b[off+i];b[off+n-2]=sum>>8;b[off+n-1]=sum;
}
int main(){
 std::array<uint8_t,512>src{},out{};
 src[2]=0x12;src[3]=0x34;src[8]=0x89;src[9]=0xab;src[10]=0xcd;src[11]=0xef;src[12]=0x7f;src[37]=100;
 signature(src,0,56,0x4441);src[448]=0x12;src[451]=0x34;src[465]=2;signature(src,448,32,0x4849);
 const auto original=src;assert(tab5::sm64::import_eeprom(src,out));assert(src==original);
 assert(out[2]==0x34 && out[3]==0x12 && out[8]==0xef && out[11]==0x89 && out[12]==0x7f && out[37]==100);
 assert(out[52]==0x41 && out[53]==0x44 && out[448]==0x34 && out[451]==0x12 && out[464]==2);
 assert(out[56]==0 && out[108]==0); // invalid backup does not acquire a signature
 out.fill(0x5a);src[12]^=1;src[448]^=1;const auto unchanged=out;
 assert(!tab5::sm64::import_eeprom(src,out));assert(out==unchanged);
 assert(!tab5::sm64::import_eeprom(std::span(src).first(511),out));
 puts("SM64 save conversion: valid blocks, endian fields, corrupt input and source preservation passed");
}
