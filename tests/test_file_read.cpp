#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "file_read.hpp"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstring>
#include <string>
#include <iostream>

struct Source {
  std::string bytes{"abcdef"};
  size_t position{0}, fail_at{999};
  bool seek_error{false}, close_error{false}, closed{false};
};
static ssize_t read_source(void *context, char *out, size_t n) {
  auto &s=*static_cast<Source *>(context);
  if(s.position>=s.fail_at){errno=EIO;return -1;}
  n=std::min({n,s.bytes.size()-s.position,s.fail_at-s.position});
  memcpy(out,s.bytes.data()+s.position,n);s.position+=n;return n;
}
static int seek_source(Source &s, int64_t &offset, int whence) {
  if(s.seek_error){errno=EIO;return -1;}
  const auto target=(whence==SEEK_END?int64_t(s.bytes.size()):whence==SEEK_CUR?int64_t(s.position):0)+offset;
  if(target<0 || target>int64_t(s.bytes.size()))return -1;
  s.position=size_t(target);offset=target;return 0;
}
static int close_source(void *context) {
  auto &s=*static_cast<Source *>(context);s.closed=true;
  if(s.close_error){errno=EIO;return -1;}return 0;
}
static FILE *open_source(Source &source) {
#ifdef __APPLE__
  return funopen(&source,
    [](void *c,char *p,int n)->int{return int(read_source(c,p,size_t(n)));},nullptr,
    [](void *c,fpos_t n,int w)->fpos_t{int64_t offset=n;return seek_source(*static_cast<Source *>(c),offset,w)?fpos_t(-1):fpos_t(offset);},
    close_source);
#else
  cookie_io_functions_t io{};io.read=read_source;io.close=close_source;
  io.seek=[](void *c,off64_t *n,int w)->int{int64_t offset=*n;int result=seek_source(*static_cast<Source *>(c),offset,w);*n=offset;return result;};
  return fopencookie(&source,"r",io);
#endif
}
int main() {
  using namespace tab5::content;
  FileBytes output;Source good;
  assert(read_owned_file(open_source(good),6,6,output)==ReadStatus::Ok && good.closed);
  assert(output.size()==6 && !memcmp(output.data(),"abcdef",6));
  auto *original=output.data();
  for(int fault=0;fault<6;++fault){
    Source input;
    if(fault==0)input.fail_at=3; // EIO after a partial read
    if(fault==1)input.seek_error=true;
    if(fault==2)input.close_error=true;
    if(fault==3)input.fail_at=6; // EIO while checking EOF after all bytes
    const auto status=read_owned_file(open_source(input),fault==4?5:6,6,output,
      fault==5?+[](size_t)->void*{return nullptr;}:std::malloc);
    assert(status==(fault==4?ReadStatus::InvalidSize:fault==5?ReadStatus::NoMemory:ReadStatus::IoError));
    assert(input.closed && output.data()==original && output.size()==6);
    assert(!memcmp(output.data(),"abcdef",6)); // no partial result escapes
  }
  Source wrong;assert(read_owned_file(open_source(wrong),100,7,output)==ReadStatus::InvalidSize);
  Source empty;empty.bytes.clear();assert(read_owned_file(open_source(empty),0,0,output)==ReadStatus::Ok && output.size()==0);
  assert(read_file("/this-file-does-not-exist-tab5",100,output)==ReadStatus::Missing);
  std::cout<<"Checked reads: partial/EIO/seek/EOF/close/size/OOM failures preserve output; all FILEs closed.\n";
}
