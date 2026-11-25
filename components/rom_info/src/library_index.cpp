#include "library_index.hpp"
#include "content_paths.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <sys/stat.h>
namespace tab5::library {
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t previous) {
  uint32_t crc = ~previous;
  for (auto byte : bytes) {
    crc ^= byte;
    for (unsigned n = 0; n < 8; ++n)
      crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int(crc & 1)));
  }
  return ~crc;
}
std::string fold(std::string text) {
  for (size_t i = 0; i < text.size(); ++i) {
    unsigned char c = text[i];
    if (c == 0xc3 && i + 1 < text.size()) {
      unsigned char d = text[i + 1];
      char r = 0;
      if ((d >= 0x80 && d <= 0x85) || (d >= 0xa0 && d <= 0xa5))
        r = 'a';
      else if (d == 0x87 || d == 0xa7)
        r = 'c';
      else if ((d >= 0x88 && d <= 0x8b) || (d >= 0xa8 && d <= 0xab))
        r = 'e';
      else if ((d >= 0x8c && d <= 0x8f) || (d >= 0xac && d <= 0xaf))
        r = 'i';
      else if ((d >= 0x92 && d <= 0x96) || (d >= 0xb2 && d <= 0xb6))
        r = 'o';
      else if ((d >= 0x99 && d <= 0x9c) || (d >= 0xb9 && d <= 0xbc))
        r = 'u';
      if (r) {
        text.replace(i, 2, 1, r);
        continue;
      }
    }
    if (c >= 'A' && c <= 'Z')
      text[i] = c - 'A' + 'a';
  }
  return text;
}
namespace {
// libstdc++ filebuf may throw from xsgetn on an SD read error even when
// the stream exception mask is empty. With IDF exceptions disabled that
// aborts the process. Keep the index's streaming reads entirely in stdio.
class FileReader {
public:
  FileReader(const std::string &path, bool &error, bool at_end=false)
      : io_error_(error), file_(fopen(path.c_str(),"rb")) {
    good_=file_!=nullptr;
    if(!file_ && errno!=ENOENT)io_error_=true;
    if(file_ && at_end && fseek(file_,0,SEEK_END))fail();
  }
  ~FileReader(){if(file_)fclose(file_);}
  FileReader(const FileReader &)=delete;
  explicit operator bool() const{return good_;}
  long tellg(){const auto n=ftell(file_);if(n<0)fail();return n;}
  void seekg(long n){if(!file_ || fseek(file_,n,SEEK_SET))fail();}
  void read(char *p,size_t n){
    count_=file_?fread(p,1,n,file_):0;
    if(count_!=n){good_=false;if(file_ && ferror(file_))io_error_=true;}
  }
  size_t gcount() const{return count_;}
  bool eof() const{return file_ && feof(file_);}
  bool error() const{return file_ && ferror(file_);}
  void clear(){if(file_){clearerr(file_);good_=true;}}
  int peek(){return file_?fgetc(file_):EOF;}
private:
  void fail(){good_=false;io_error_=true;}
  bool &io_error_;
  FILE *file_{nullptr};
  bool good_{false};
  size_t count_{0};
};
uint32_t u32(const uint8_t *p) {
  return p[0] | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
uint64_t u64(const uint8_t *p) { return u32(p) | (uint64_t(u32(p + 4)) << 32); }
bool utf8(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t c = (unsigned char)s[i++];
    if (c < 128) {
      if (!c)
        return false;
      continue;
    }
    unsigned n, minimum;
    if (c >= 0xc2 && c <= 0xdf) {
      c &= 31;
      n = 1;
      minimum = 128;
    } else if (c >= 0xe0 && c <= 0xef) {
      c &= 15;
      n = 2;
      minimum = 2048;
    } else if (c >= 0xf0 && c <= 0xf4) {
      c &= 7;
      n = 3;
      minimum = 65536;
    } else
      return false;
    if (i + n > s.size())
      return false;
    while (n--) {
      unsigned char d = s[i++];
      if ((d & 0xc0) != 0x80)
        return false;
      c = (c << 6) | (d & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return false;
  }
  return true;
}
} // namespace
Page read_index(const std::string &path, const Query &q,
                const std::function<bool()> &cancel) {
  Page result;
  if(q.page>100000/page_size)return result;
  result.error = "Indice invalido ou desatualizado; reconstrua no computador";
  FileReader file(path,result.io_error,true);
  if (!file)
    return result;
  auto size = file.tellg();
  if (size < 64 || size > 64 * 1024 * 1024)
    return result;
  std::array<uint8_t, 64> head{};
  file.seekg(0);
  file.read((char *)head.data(), head.size());
  const uint32_t count = u32(head.data() + 12), bytes = u32(head.data() + 16);
  if (!file || memcmp(head.data(), "T5LIBI01", 8) ||
      u32(head.data() + 8) != 1 || count > 100000 || bytes != size)
    return result;
  for (size_t i = 40; i < 64; ++i)
    if (head[i])
      return result;
  std::array<uint8_t, 1024> scratch{};
  FileReader source(content::resolve("library/games.json"),result.io_error);
  uint64_t source_bytes = 0;
  uint32_t source_crc = 0;
  if (source) {
    while (source) {
      if (cancel && cancel()) {
        result.cancelled = true;
        return result;
      }
      source.read((char *)scratch.data(), scratch.size());
      auto n = source.gcount();
      if (n > 0) {
        source_bytes += n;
        source_crc = crc32(std::span(scratch.data(), n), source_crc);
      }
      if (source_bytes > 64 * 1024 * 1024)
        return result;
    }
    if (!source.eof())
      return result;
  }
  if (source_bytes != u64(head.data() + 24) ||
      source_crc != u64(head.data() + 32))
    return result;
  uint32_t crc = 0;
  while (file) {
    if (cancel && cancel()) {
      result.cancelled = true;
      return result;
    }
    file.read((char *)scratch.data(), scratch.size());
    const auto n = file.gcount();
    if (n > 0)
      crc = crc32(std::span(scratch.data(), n), crc);
  }
  if (!file.eof() || crc != u32(head.data() + 20))
    return result;
  file.clear();
  file.seekg(64);
  std::string needle = q.text;
  const std::string prefix = std::string(content::root) + "/";
  if (needle.starts_with(prefix))
    needle.erase(0, prefix.size());
  const auto search = fold(needle);
  size_t matches = 0;
  const size_t first = q.page * page_size;
  for (uint32_t i = 0; i < count; ++i) {
    if (cancel && cancel()) {
      result.cancelled = true;
      return result;
    }
    std::array<uint8_t, 12> lengths{};
    file.read((char *)lengths.data(), lengths.size());
    unsigned len[6]{};
    size_t total = 0;
    constexpr unsigned limits[] = {16, 160, 240, 240, 2048, 80};
    for (unsigned n = 0; n < 6; ++n) {
      len[n] = lengths[2 * n] | (unsigned(lengths[2 * n + 1]) << 8);
      if (len[n] > limits[n])
        return result;
      total += len[n];
    }
    if (!file || total > 2784)
      return result;
    std::string text(total, '\0');
    file.read(text.data(), total);
    if (!file || !utf8(text))
      return result;
    std::string fields[6];
    size_t off = 0;
    for (unsigned n = 0; n < 6; ++n) {
      fields[n] = text.substr(off, len[n]);
      off += len[n];
    }
    const auto &[system, title, rom, cover, description, id] = fields;
    if (title.empty() || !content::valid_relative(rom) ||
        (!cover.empty() && !content::valid_relative(cover)))
      return result;
    Emulator platform;
    if (system == "gb")
      platform = Emulator::GAMEBOY;
    else if (system == "gbc")
      platform = Emulator::GAMEBOY_COLOR;
    else if (system == "gba")
      platform = Emulator::GBA;
    else if (system == "snes")
      platform = Emulator::SNES;
    else if (system == "megadrive")
      platform = Emulator::SEGA_MEGA_DRIVE;
    else if (system == "n64")
      platform = Emulator::N64;
    else
      return result;
#ifdef ESP_PLATFORM
#ifndef ENABLE_N64
    if(platform==Emulator::N64)continue;
#endif
#ifndef ENABLE_GBA
    if(platform==Emulator::GBA)continue;
#endif
#ifndef ENABLE_GENESIS
    if(platform==Emulator::SEGA_MEGA_DRIVE)continue;
#endif
#ifndef ENABLE_GBC
    if (platform == Emulator::GAMEBOY || platform == Emulator::GAMEBOY_COLOR)
      continue;
#endif
#ifndef ENABLE_SNES
    if (platform == Emulator::SNES)
      continue;
#endif
#endif
    const auto dot = rom.find_last_of('.');
    if (dot == rom.npos)
      return result;
    auto ext = rom.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if ((system == "gb" && ext != ".gb") ||
        (system == "gbc" && ext != ".gbc") ||
        (system == "gba" && ext != ".gba") ||
        (system == "snes" && ext != ".sfc" && ext != ".smc") ||
        (system == "megadrive" && ext != ".md" && ext != ".gen" &&
         ext != ".smd") ||
        (system == "n64" && ext != ".z64" && ext != ".n64" && ext != ".v64"))
      return result;
    const auto path = content::resolve(rom);
    if (!q.system.empty() && q.system != system)
      continue;
    const auto &list = q.view == 2 ? q.recent : q.favorites;
    if (q.view && std::find(list.begin(), list.end(), path) == list.end())
      continue;
    if (!search.empty() && (q.exact
            ? (fold(title) != search && fold(rom) != search)
            : fold(title + " " + description + " " + rom).find(search) == std::string::npos))
      continue;
    if (matches >= first && result.games.size() < page_size)
      result.games.push_back({title,
                              cover.empty() ? "" : content::resolve(cover),
                              path, platform, description, id});
    ++matches;
  }
  if (file.peek() != EOF || file.error())
    return result;
  result.total = matches;
  result.catalog_total = count;
  result.number = q.page;
  result.valid = true;
  result.error.clear();
  return result;
}
} // namespace tab5::library
