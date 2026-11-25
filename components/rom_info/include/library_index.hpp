#pragma once
#include "rom_info.hpp"
#include <functional>
#include <span>
namespace tab5::library {
inline constexpr size_t page_size = 64;
struct Query {
  std::string text, system;
  int view{0};
  size_t page{0};
  bool exact{false};
  std::vector<std::string> favorites, recent;
};
struct Page {
  std::vector<RomInfo> games;
  size_t total{0}, catalog_total{0}, number{0};
  bool valid{false}, cancelled{false}, io_error{false};
  std::string error;
};
std::string fold(std::string text);
uint32_t crc32(std::span<const uint8_t> bytes, uint32_t previous = 0);
// A rebuildable PC-generated index; pages and parser scratch are bounded.
// Each query streams the file; no vector/tree proportional to catalog size.
Page read_index(const std::string &path, const Query &query,
                const std::function<bool()> &cancel = {});
} // namespace tab5::library
