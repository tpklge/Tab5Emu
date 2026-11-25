#include "md_sram.hpp"
#include "md_pad_protocol.h"
#include "md_rom.hpp"
#include "md_state.hpp"
#include "md_tekken.hpp"
#include <cassert>
#include <iostream>
#include <memory>
int main() {
  {
    auto rom=std::make_unique<std::array<uint8_t,0x200000>>();
    (*rom)[0x1ffffe]=0xc2;(*rom)[0x1fffff]=0xf0;
    tab5::md::TekkenProtection p;p.configure(rom->data(),rom->size());assert(p.enabled);
    assert(p.rom_address(0x1fffff)==0x1fffff && p.rom_address(0x200000)==0);
    assert(p.rom_address(0x3fffff)==0x1fffff && p.rom_address(0x400002)==0x400002);
    uint32_t value=123;
    for(unsigned bits=0;bits<16;++bits){
      assert(p.write(0x400000,255));
      for(unsigned bit=0;bit<4;++bit)p.write(0x400004+bit*2,(bits>>bit)&1);
      assert(p.read(0x400002,value) && value==uint16_t(bits-1));
      p.write(0x40000c,1);p.write(0x40000e,1);assert(p.bits==bits);
    }
    assert(!p.write(0x410004,1) && !p.read(0x400004,value));
    // Byte transactions used by the real game's serial protection routine.
    // Its first check feeds 3 and expects 2; the boot check feeds 13 -> 12.
    for(unsigned input: {3u,13u,0u,15u}) {
      assert(p.write_byte(0x400000,15));
      for(unsigned bit=0;bit<4;++bit)
        if(input&(1u<<bit))assert(p.write_byte(0x400004+bit*2,1));
      assert(p.write_byte(0x40000c,1));
      assert(p.read_byte(0x400002,value) && value==((input-1)&255));
    }
    (*rom)[0x18e]=1;p.configure(rom->data(),rom->size());assert(!p.enabled);
  }
  static_assert(sizeof(Tab5MdPad)==4);
  Tab5MdPad pad{0,0,15,1};
  auto rise=[&]{tab5_md_pad_th(&pad,0,64);};
  assert(tab5_md_pad_read(&pad,64,255)==127);rise();rise();
  assert(tab5_md_pad_read(&pad,0,255)==0x30);rise();pad.extra=11;
  assert(tab5_md_pad_read(&pad,64,255)==0x7b);
  assert(tab5_md_pad_read(&pad,0,255)==0x3f);rise();
  assert(tab5_md_pad_read(&pad,64,255)==0x7f);
  for(unsigned n=0;n<26;++n)tab5_md_pad_line(&pad);
  assert(pad.phase==0);pad.enabled=0;rise();assert(pad.phase==0);
  Tab5MdPad saved_pad{6,3,5,1};
  tab5_md_pad_restore(&pad,&saved_pad);
  assert(pad.enabled==0 && pad.phase==0 && pad.age==0 && pad.extra==15);
  pad.enabled=1;tab5_md_pad_restore(&pad,&saved_pad);
  assert(pad.enabled==1 && pad.phase==6 && pad.age==3 && pad.extra==5);
  std::vector<uint8_t> raw(32768),smd(32768+512);
  for(size_t n=0;n<raw.size();++n)raw[n]=(n*13)^(n>>8);
  smd[1]=3;smd[8]=0xaa;smd[9]=0xbb;
  for(size_t off=0;off<raw.size();off+=16384)for(size_t n=0;n<8192;++n){smd[512+off+n]=raw[off+2*n+1];smd[512+off+8192+n]=raw[off+2*n];}
  auto original=smd;size_t length=smd.size();assert(tab5::md::decode_smd(smd.data(),length));
  assert(length==raw.size()&&std::equal(raw.begin(),raw.end(),smd.begin()));
  length=original.size()-1;assert(!tab5::md::decode_smd(original.data(),length));

  using tab5::md::LinearSram;
  auto ram = std::make_unique<LinearSram>();
  std::array<uint8_t, 1024> rom{};
  assert(ram->configure(rom.data(), rom.size()) == LinearSram::Kind::None);
  rom[0x1b0] = 'R';
  rom[0x1b1] = 'A';
  auto put = [&](size_t off, uint32_t value) {
    for (int n = 0; n < 4; ++n)
      rom[off + n] = value >> (24 - n * 8);
  };
  put(0x1b4, 0x200001);
  put(0x1b8, 0x203fff);
  assert(ram->configure(rom.data(), rom.size()) == LinearSram::Kind::Linear &&
         ram->size == 8192 && ram->stride == 2);
  uint32_t value = 0;
  assert(ram->write8(0x200001, 0x54));
  assert(ram->read8(0x200001, value) && value == 0x54);
  assert(!ram->read8(0x200000, value));
  assert(ram->read8(0x200002, value) && value == 255);
  ram->write8(0xa130f1, 3);
  ram->write8(0x200001, 0x22);
  assert(ram->read8(0x200001, value) && value == 0x54);
  ram->write8(0xa130f1, 0);
  assert(!ram->read8(0x200001, value));
  ram->write8(0xa130f1, 1);
  assert(ram->read8(0x200001, value) && value == 0x54);
  put(0x1b4, 0x200000);
  put(0x1b8, 0x203fff);
  assert(ram->configure(rom.data(), rom.size()) == LinearSram::Kind::Linear &&
         ram->size == 16384 && ram->stride == 1);
  put(0x1b8, 0x200000);
  assert(ram->configure(rom.data(), rom.size()) ==
         LinearSram::Kind::Unsupported);
  put(0x1b8, 0x800000);
  assert(ram->configure(rom.data(), rom.size()) ==
         LinearSram::Kind::Unsupported);
  assert(ram->configure(rom.data(), 10) == LinearSram::Kind::Unsupported);
  std::array<uint8_t,66> records{};
  records[0]='a';tab5::md::put32(records.data()+28,1);
  records[33]='b';tab5::md::put32(records.data()+61,1);
  auto incoming=records;incoming[32]=47;incoming[65]=93;
  assert(tab5::md::same_records(records,incoming));
  incoming[33]='a';assert(!tab5::md::same_records(records,incoming));
  incoming=records;tab5::md::put32(incoming.data()+28,65535);
  assert(!tab5::md::same_records(records,incoming));
  assert(!tab5::md::same_records(records,std::span(records).first(65)));
  records[28]=0;assert(!tab5::md::same_records(records,records));
  std::cout << "MD linear SRAM: header bounds, byte lanes, enable, write "
               "protection, unsupported mappers and state record validation OK\n";
}
