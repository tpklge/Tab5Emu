#include "gb_memory.hpp"
#include "gbc_shared_memory.hpp"
#include "shared_memory.h"
#include <cassert>
#include <cstdlib>
#include <set>
#include <vector>
#include <iostream>
extern "C" {
#include "gnuboy/defs.h"
#include "gnuboy/loader.h"
#include "gnuboy/lcd.h"
#include "gnuboy/cpu.h"
#include "gnuboy/mem.h"
#include "gnuboy/hw.h"
#include "gnuboy/rtc.h"
#include "gnuboy/pcm.h"
struct lcd *lcd;
struct gbc_scan *scan;
byte *bgdup, *gbc_filebuf;
struct ram ram;
struct rom rom;
struct mbc mbc;
struct hw hw;
struct rtc rtc;
struct pcm pcm;
void die(char *,...){assert(false);}
}
static int calls=0,fail=-1;
static std::set<void *> live;
extern "C" void *heap_caps_malloc(size_t size,unsigned) {
  if(calls++==fail)return nullptr;
  void *p=std::malloc(size);assert(p);live.insert(p);return p;
}
extern "C" void heap_caps_free(void *p){if(p){assert(live.erase(p)==1);std::free(p);}}
extern "C" size_t heap_caps_get_free_size(unsigned){return 1024*1024;}
static void cleared() {
  assert(!shared_num_bytes_allocated() && live.empty());
  assert(!cpu && !lcd && !scan && !bgdup && !gbc_filebuf && !pcm.buf && !ram.ibank);
}
int main() {
  for(int n=0;n<8;++n){
    calls=0;fail=n;assert(!gb_memory_init());cleared();
    fail=-1;assert(gb_memory_init());assert(live.size()==8);
    assert(!gb_memory_init());assert(live.size()==8); // no second live core
    gbc_free_shared_memory();gbc_free_shared_memory();cleared();
  }
  fail=-1;auto *other=shared_malloc(16);assert(other);
  assert(!gb_memory_init());assert(live.size()==1);shared_mem_clear();cleared();
  std::vector<unsigned char> image(32768);image[0x147]=3;image[0x149]=4;
  for(bool failed:{true,false}){
    fail=-1;assert(gb_memory_init());unsigned char *wram=nullptr;gbc_get_memory_regions(nullptr,&wram,nullptr);
    ram.ibank=wram;
    fail=failed?calls:-1;
    assert((rom_load(image.data(),image.size())!=0)==failed);
    if(failed)assert(!ram.sbank);
    else {assert(ram.sbank && mbc.ramsize==16);ram.sbank[15][8191]=42;}
    loader_unload();assert(!ram.sbank);gbc_free_shared_memory();cleared();
  }
  std::cout<<"GB: all eight allocation failures roll back; retry, SRAM OOM and max SRAM pass with no leaks.\n";
}
