#pragma once
#include <cstddef>
#include <cstdint>
namespace tab5::md {
// Hardware behavior documented by Genesis Plus GX's md_cart.c. This local
// four-bit latch models the cartridge, without modifying the game's ROM.
struct TekkenProtection {
  bool enabled{false};
  uint8_t bits{0};
  uint32_t rom_address(uint32_t address) const {
    // The 2 MiB ROM ignores A21 in the normal 0..4 MiB cartridge window.
    // Keep the separate protection register bank at 0x400000 intact.
    return enabled && address<0x400000 ? address&0x1fffff : address;
  }
  void configure(const uint8_t *rom, size_t size) {
    enabled=false;bits=0;
    if (!rom || size!=0x200000 || rom[0x18e] || rom[0x18f]) return;
    uint32_t sum=0;
    for(size_t i=512;i<size;i+=2)sum+=(uint32_t(rom[i])<<8)|rom[i+1];
    enabled=uint16_t(sum)==0xc2f0;
  }
  bool read(uint32_t address,uint32_t &value) const {
    if(!enabled || (address&0xff0000)!=0x400000 || (address&14)!=2)return false;
    value=uint16_t(uint16_t(bits)-1);return true;
  }
  bool read_byte(uint32_t address,uint32_t &value) const {
    if(!read(address,value))return false;
    value&=255;return true;
  }
  bool write_byte(uint32_t address,uint32_t value) {
    // The game's protection routine uses MOVE.B for every register access.
    // This peripheral decodes the port, rather than normal ROM byte lanes.
    return write(address,value&255);
  }
  bool write(uint32_t address,uint32_t value) {
    if(!enabled || (address&0xff0000)!=0x400000)return false;
    const auto port=address&14;
    if(!port)bits=0;
    else if(port>=4 && port<=10 && (value&1))bits|=uint8_t(1u<<((port-4)/2));
    return true;
  }
};
}
