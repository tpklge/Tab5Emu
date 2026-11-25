#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
namespace tab5::sm64 {
// SM64's EEPROM stores numeric fields in big endian; the native port uses
// little endian. Only checksum-valid blocks are imported, never the source.
inline bool import_eeprom(std::span<const uint8_t> source, std::array<uint8_t,512>&out) {
  if(source.size()<512)return false;
  std::array<uint8_t,512> staged{};bool valid=false;
  auto block=[&](unsigned offset,unsigned size,uint16_t magic,bool menu){
    const auto b=source.subspan(offset,size);
    unsigned sum=0;for(unsigned n=0;n<size-2;++n)sum+=b[n];
    if(((b[size-4]<<8)|b[size-3])!=magic || ((b[size-2]<<8)|b[size-1])!=uint16_t(sum))return;
    std::copy(b.begin(),b.end(),staged.begin()+offset);
    auto swap=[&](unsigned pos,unsigned count){std::reverse(staged.begin()+offset+pos,staged.begin()+offset+pos+count);};
    if(menu){for(unsigned n=0;n<16;n+=4)swap(n,4);swap(16,2);}
    else{for(unsigned n=2;n<8;n+=2)swap(n,2);swap(8,4);}
    swap(size-4,2);swap(size-2,2);valid=true;
  };
  for(unsigned n=0;n<8;++n)block(n*56,56,0x4441,false);
  block(448,32,0x4849,true);block(480,32,0x4849,true);
  if(valid)out=staged;
  return valid;
}
}
