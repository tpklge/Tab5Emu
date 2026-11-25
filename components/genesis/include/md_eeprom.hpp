#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace tab5::md {
// Original implementation from the Xicor X24C01/X24C02 wire specifications.
// Only the identified Sega, EA and Acclaim 16M boards are enabled here.
// Storage is supplied by the existing cartridge buffer; no allocation.
struct SerialEeprom {
  enum class Board : uint8_t { None, Sega, EA, Acclaim16 };
  enum Phase : uint8_t { Idle, Command, Address, Write, Read, MasterAck, Ack };
  static constexpr size_t snapshot_bytes=32;
  Board board{};
  uint8_t *data=nullptr;
  uint8_t scl=1, sda=1, low=0, phase=Idle, next=Idle, bits=0, shift=0;
  uint8_t pointer=0, ack_step=0, master_ack=0, page_base=0, pending=0;
  std::array<uint8_t,4> page{};
  size_t size() const { return board==Board::None?0:board==Board::Acclaim16?256:128; }
  static Board identify(const uint8_t *rom,size_t length) {
    if(!rom || length<0x200)return Board::None;
    // A linear SRAM patch takes precedence over the original product code.
    auto be=[](const uint8_t *p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
    if(rom[0x1b0]=='R' && rom[0x1b1]=='A' && rom[0x1b2]!=0xe8 &&
       be(rom+0x1b8)>be(rom+0x1b4)+1)return Board::None;
    std::string_view code(reinterpret_cast<const char*>(rom+0x180),14);
    if(code.starts_with("GM"))code.remove_prefix(2);
    while(!code.empty() && code.front()==' ')code.remove_prefix(1);
    auto match=[&](std::string_view id){return code.starts_with(id) &&
      (code.size()==id.size() || code[id.size()]==' ' || code[id.size()]=='-' || code[id.size()]=='\0');};
    for(auto id:{"T-50176","T-50396","T-50446","T-50516","T-50606"})
      if(match(id))return Board::EA;
    for(auto id:{"T-12046","T-12053","MK-1215","MK-1228","G-5538","PR-1993",
                 "G-4060","00001211","00004076","G-4524","00054503"})
      if(match(id))return Board::Sega;
    for(auto id:{"T-81033","T-081326"})if(match(id))return Board::Acclaim16;
    return Board::None;
  }
  void reset() {
    scl=sda=1;low=0;phase=next=Idle;bits=shift=pointer=ack_step=master_ack=page_base=pending=0;page.fill(0);
  }
  void configure(Board selected,uint8_t *storage) {board=selected;data=storage;reset();}
  bool mapped(uint32_t a) const {return board!=Board::None && a>=0x200000 && a<0x400000;}
  unsigned output_bit() const {return board==Board::EA?7:board==Board::Acclaim16?1:0;}
  bool read8(uint32_t a,uint32_t &v) const {
    if(!mapped(a))return false;
    v=(a&1)?unsigned(!low)<<output_bit():0xff;return true;
  }
  bool read16(uint32_t a,uint32_t &v) const {
    if(!mapped(a))return false;
    v=unsigned(!low)<<output_bit();return true;
  }
  bool write8(uint32_t a,uint32_t v) {
    if(!mapped(a))return false;
    if((a&1) || board==Board::Acclaim16)write_pins(v);
    return true;
  }
  bool write16(uint32_t a,uint32_t v) {
    if(!mapped(a))return false;
    write_pins(v);return true;
  }
  void write_pins(uint32_t v) {clock((v>>(board==Board::EA?6:1))&1,(v>>(board==Board::EA?7:0))&1);}
  void received(uint8_t value) {
    next=Idle;
    if(phase==Command) {
      if(board==Board::Acclaim16) {
        if((value&0xfe)!=0xa0){phase=Idle;low=0;return;}
        next=(value&1)?Read:Address;
      } else {pointer=value>>1;next=(value&1)?Read:Write;}
    } else if(phase==Address) {pointer=value;next=Write;}
    else if(phase==Write) {
      if(!pending)page_base=pointer&0xfc;
      page[pointer&3]=value;pending|=1u<<(pointer&3);
      pointer=(pointer&0xfc)|((pointer+1)&3);next=Write;
    }
    phase=Ack;ack_step=0;bits=0;
  }
  void clock(uint8_t c,uint8_t d) {
    if(scl && c && d!=sda) {
      if(!d) {phase=Command;bits=shift=low=pending=ack_step=0;}
      else {
        if(data)for(unsigned i=0;i<4;++i)if(pending&(1u<<i))data[page_base+i]=page[i];
        phase=Idle;low=pending=0;
      }
    } else if(!scl && c) {
      switch(phase) {
      case Command:case Address:case Write:
        shift=uint8_t((shift<<1)|d);if(++bits==8)received(shift);break;
      case Ack:if(ack_step==1)ack_step=2;break;
      case Read:++bits;break;
      case MasterAck:master_ack=!d;break;
      default:break;
      }
    } else if(scl && !c) {
      if(phase==Ack) {
        if(ack_step==0){low=1;ack_step=1;}
        else if(ack_step==2){phase=next;bits=shift=0;low=phase==Read?!(data[pointer]&0x80):0;}
      } else if(phase==Read) {
        if(bits>=8){pointer=uint8_t((pointer+1)%size());phase=MasterAck;low=0;}
        else low=!(data[pointer]&(0x80>>bits));
      } else if(phase==MasterAck) {
        phase=master_ack?Read:Idle;bits=0;low=phase==Read?!(data[pointer]&0x80):0;
      }
    }
    scl=c;sda=d;
  }
  std::array<uint8_t,snapshot_bytes> snapshot() const {
    std::array<uint8_t,snapshot_bytes> b{};
    b[0]='E';b[1]='E';b[2]=1;b[3]=uint8_t(board);
    const uint8_t values[]={scl,sda,low,phase,next,bits,shift,pointer,ack_step,master_ack,page_base,pending};
    std::memcpy(b.data()+4,values,sizeof(values));std::memcpy(b.data()+16,page.data(),4);return b;
  }
  bool restore(const uint8_t *b) {
    if(b[0]!='E'||b[1]!='E'||b[2]!=1||b[3]!=uint8_t(board)||board==Board::None ||
       b[4]>1||b[5]>1||b[6]>1||b[7]>Ack||b[8]>Ack||b[9]>8||b[11]>=size()||
       b[12]>2||b[13]>1||b[14]>=size()||(b[14]&3)||b[15]>15 ||
       (b[7]==Read && b[9]==8 && !b[4]) ||
       ((b[7]==Command||b[7]==Address||b[7]==Write) && b[9]>7) ||
       (b[7]==Ack && b[8]!=Address && b[8]!=Write && b[8]!=Read))return false;
    for(size_t i=20;i<snapshot_bytes;++i)if(b[i])return false;
    scl=b[4];sda=b[5];low=b[6];phase=b[7];next=b[8];bits=b[9];shift=b[10];pointer=b[11];
    ack_step=b[12];master_ack=b[13];page_base=b[14];pending=b[15];std::memcpy(page.data(),b+16,4);return true;
  }
};
} // namespace tab5::md
