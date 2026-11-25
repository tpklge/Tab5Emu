#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tab5::gb {
inline size_t state_bytes(bool color,unsigned ram_banks) {
  if (!ram_banks || ram_banks>16) return 0;
  return (1+(color ? 8 : 2)+(color ? 4 : 2)+ram_banks*2)*4096;
}
// gnuboy's FILE loader trusts its header's copy offsets and termination.
// Validate the current raw format before invoking it; old incompatible state
// formats must be converted, not passed through its unchecked legacy paths.
inline bool valid_state_header(const uint8_t *data,size_t size,bool color,unsigned banks) {
  if (!data || size<4096 || !state_bytes(color,banks)) return false;
  const char *tags[]={"GbSs","cgb ","iram","vram","sram","hi  ","pal ","oam ","wav "};
  const uint32_t wanted[]={0x105,static_cast<uint32_t>(color),1,1+(color?8u:2u),
    1+(color?8u:2u)+(color?4u:2u),4096-768,4096-512,4096-256,4096-784};
  unsigned found=0;
  for (size_t offset=0;offset+8<=4096;offset+=8) {
    const auto *p=data+offset;
    if (!(p[0]|p[1]|p[2]|p[3])) return found==(1u<<9)-1;
    const uint32_t value=p[4]|(uint32_t(p[5])<<8)|(uint32_t(p[6])<<16)|(uint32_t(p[7])<<24);
    for (unsigned i=0;i<9;++i) if (memcmp(p,tags[i],4)==0) {
      if ((found&(1u<<i)) || value!=wanted[i]) return false;
      found|=1u<<i;
    }
    if (memcmp(p,"hram",4)==0 && value!=0) return false;
  }
  return false;
}
}
