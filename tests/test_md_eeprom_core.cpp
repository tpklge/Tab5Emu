// Run original serial-save fixtures through the actual staged M68000 core.
#include "md_eeprom.hpp"
extern "C" {
#include "m68k.h"
#include "gwenesis_savestate.h"
unsigned char *ROM_DATA,*M68K_RAM;
uint32_t tab5_md_fast_rom_end;
}
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
static tab5::md::SerialEeprom chip;
extern "C" {
int vdp_68k_irq_ack(int){return 0x18;}
unsigned int m68k_read_memory_8(unsigned int a){
  a&=0xffffff;uint32_t v;
  if(chip.read8(a,v))return v;
  if(a>=0xe00000)return FETCH8RAM(a);
  return a<tab5_md_fast_rom_end?FETCH8ROM(a):255;
}
unsigned int m68k_read_memory_16(unsigned int a){uint32_t v;if(chip.read16(a,v))return v;return m68k_read_memory_8(a)*256+m68k_read_memory_8(a+1);}
unsigned int m68k_read_memory_32(unsigned int a){return m68k_read_memory_16(a)*65536u+m68k_read_memory_16(a+2);}
void m68k_write_memory_8(unsigned int a,unsigned int v){if(chip.write8(a,v))return;if((a&0xffffff)>=0xe00000)WRITE8RAM(a,v);}
void m68k_write_memory_16(unsigned int a,unsigned int v){if(chip.write16(a,v))return;m68k_write_memory_8(a,v>>8);m68k_write_memory_8(a+1,v);}
void m68k_write_memory_32(unsigned int a,unsigned int v){m68k_write_memory_16(a,v>>16);m68k_write_memory_16(a+2,v);}
SaveState*saveGwenesisStateOpenForWrite(const char*){return nullptr;}
SaveState*saveGwenesisStateOpenForRead(const char*){return nullptr;}
void saveGwenesisStateSet(SaveState*,const char*,int){}
int saveGwenesisStateGet(SaveState*,const char*){return 0;}
void saveGwenesisStateSetBuffer(SaveState*,const char*,const void*,int){}
void saveGwenesisStateGetBuffer(SaveState*,const char*,void*p,int n){std::memset(p,0,n);}
}
int main(int argc,char **argv){
  assert(argc==2);
  ROM_DATA=static_cast<unsigned char*>(std::calloc(1,32768));
  M68K_RAM=static_cast<unsigned char*>(std::calloc(1,65536));
  m68k=static_cast<m68ki_cpu_core*>(std::calloc(1,sizeof(*m68k)));
  unsigned index=0;
  for(const auto *board:{"sega","ea","acclaim16"}){
    ++index;std::ifstream f(std::string(argv[1])+"/"+board+".md",std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)),{});assert(rom.size()==32768);
    const auto selected=tab5::md::SerialEeprom::identify(rom.data(),rom.size());assert(unsigned(selected)==index);
    for(size_t i=0;i<rom.size();++i)ROM_DATA[i^1]=rom[i];tab5_md_fast_rom_end=rom.size();
    std::array<uint8_t,256> battery;battery.fill(255);chip.configure(selected,battery.data());
    for(unsigned boot=1;boot<=2;++boot){
      chip.reset();std::memset(M68K_RAM,0,65536);std::memset(m68k,0,sizeof(*m68k));
      m68k_init();m68k_pulse_reset();m68k_run(500000);
      assert(FETCH8RAM(0xff0000)==boot);
      assert(battery[0]==boot && battery[1]=='T' && battery[2]=='5' && battery[3]==index);
    }
  }
  std::free(m68k);std::free(M68K_RAM);std::free(ROM_DATA);
  std::cout<<"Actual M68000: Sega/EA/Acclaim16 EEPROM guest read/write and second boot passed\n";
}
