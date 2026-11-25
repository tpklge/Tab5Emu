#include "md_eeprom.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>
using tab5::md::SerialEeprom;
struct Bus {
  SerialEeprom chip;
  std::array<uint8_t,256> memory;
  explicit Bus(SerialEeprom::Board board) {memory.fill(255);chip.configure(board,memory.data());}
  void pins(unsigned c,unsigned d) {
    const auto value=chip.board==SerialEeprom::Board::EA?(c<<6)|(d<<7):(c<<1)|d;
    assert(chip.write8(0x200001,value));
  }
  void start(){pins(0,1);pins(1,1);pins(1,0);pins(0,0);}
  void stop(){pins(0,0);pins(1,0);pins(1,1);}
  bool send(unsigned b) {
    for(unsigned bit=0;bit<8;++bit){auto d=(b>>(7-bit))&1;pins(0,d);pins(1,d);pins(0,d);}
    pins(0,1);pins(1,1);uint32_t v;assert(chip.read16(0x200000,v));const bool ack=!(v&(1u<<chip.output_bit()));pins(0,1);return ack;
  }
  unsigned read(bool ack) {
    unsigned value=0;
    for(unsigned bit=0;bit<8;++bit){pins(1,1);uint32_t v;chip.read8(0x200001,v);value=(value<<1)|((v>>chip.output_bit())&1);pins(0,1);}
    pins(0,!ack);pins(1,!ack);pins(0,!ack);return value;
  }
  void address(unsigned a,bool read=false) {
    if(chip.board==SerialEeprom::Board::Acclaim16){assert(send(0xa0));assert(send(a));if(read){start();assert(send(0xa1));}}
    else assert(send((a<<1)|read));
  }
};
int main() {
  for(auto board:{SerialEeprom::Board::Sega,SerialEeprom::Board::EA,SerialEeprom::Board::Acclaim16}) {
    Bus b(board);uint32_t value;
    assert(!b.chip.read8(0x400000,value));assert(!b.chip.write16(0x1ffffe,0));
    b.start();b.address(6);assert(b.send(0x12));assert(b.send(0x34));assert(b.send(0x56));assert(b.send(0x78));assert(b.send(0x9a));
    assert(b.memory[6]==255); // A write takes effect only at STOP.
    b.stop();assert(b.memory[4]==0x56 && b.memory[5]==0x78 && b.memory[6]==0x9a && b.memory[7]==0x34);
    b.start();b.address(4,true);assert(b.read(true)==0x56);assert(b.read(true)==0x78);assert(b.read(false)==0x9a);b.stop();
    b.memory[b.chip.size()-1]=0xab;b.memory[0]=0xcd;
    b.start();b.address(b.chip.size()-1,true);assert(b.read(true)==0xab);assert(b.read(false)==0xcd);b.stop();
    // Mid-transaction state: retain a pending page, bit counter and ACK.
    b.start();b.address(11);assert(b.send(0x42));
    auto snapshot=b.chip.snapshot();auto saved_memory=b.memory;
    assert(b.send(0x51));b.stop();b.memory=saved_memory;
    b.chip.reset();assert(b.chip.restore(snapshot.data()));assert(b.send(0x51));b.stop();
    assert(b.memory[11]==0x42 && b.memory[8]==0x51);
    b.start();b.address(8,true);b.pins(1,1);b.pins(0,1);snapshot=b.chip.snapshot();
    auto resumed=b.chip;assert(resumed.restore(snapshot.data()));assert(resumed.snapshot()==snapshot);
    // A malformed snapshot must not change live protocol state.
    for(unsigned i:{0u,2u,3u,4u,7u,9u,12u,14u,15u,31u}){
      auto bad=snapshot;bad[i]=255;const auto before=b.chip.snapshot();assert(!b.chip.restore(bad.data()));assert(before==b.chip.snapshot());
    }
    b.chip.reset();b.start();b.address(20);assert(b.send(0x99));b.chip.reset();assert(b.memory[20]==255);
    if(board==SerialEeprom::Board::Acclaim16){b.start();assert(!b.send(0xa2));b.stop();}
  }
  std::array<uint8_t,512> rom{};auto identify=[&](const char *id){std::fill_n(rom.data()+0x180,14,' ');std::copy_n(id,std::char_traits<char>::length(id),rom.data()+0x180);return SerialEeprom::identify(rom.data(),rom.size());};
  assert(identify("GM T-50176-00")==SerialEeprom::Board::EA);
  assert(identify("GM MK-1215 -00")==SerialEeprom::Board::Sega);
  assert(identify("GM T-081326-00")==SerialEeprom::Board::Acclaim16);
  assert(identify("GM T-501760-00")==SerialEeprom::Board::None);
  identify("GM T-12046 -00");rom[0x1b0]='R';rom[0x1b1]='A';rom[0x1b8]=0x00;rom[0x1b9]=0x20;rom[0x1ba]=0x3f;rom[0x1bb]=0xff;
  assert(SerialEeprom::identify(rom.data(),rom.size())==SerialEeprom::Board::None);
  std::cout<<"MD EEPROM: Sega/EA X24C01, Acclaim16 X24C02; page/ACK/STOP/restart/wrap/snapshot passed\n";
}
