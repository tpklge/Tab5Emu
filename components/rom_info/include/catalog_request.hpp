#pragma once
#include "library_index.hpp"
#include <charconv>
namespace tab5::library {
// offset NUL system NUL search, with no trailing NUL. Kept outside the
// transport so malformed requests can be tested on the host.
inline bool catalog_request(std::string_view payload, Query &query, size_t &offset) {
  const auto first = payload.find('\0');
  const auto second = first == payload.npos ? payload.npos : payload.find('\0', first+1);
  if (first == payload.npos || second == payload.npos || payload.find('\0', second+1) != payload.npos) return false;
  size_t parsed = 0;
  const auto number = std::from_chars(payload.data(), payload.data()+first, parsed);
  if (number.ec != std::errc{} || number.ptr != payload.data()+first || parsed > 100000) return false;
  Query staged;
  staged.system = payload.substr(first+1, second-first-1);
  if (staged.system == "genesis") staged.system = "megadrive";
  if (staged.system != "" && staged.system != "gb" && staged.system != "gbc" && staged.system != "gba" && staged.system != "snes" && staged.system != "megadrive" && staged.system != "n64") return false;
  staged.text = payload.substr(second+1);
  if (staged.text.size() > 128) return false;
  staged.page = parsed / page_size;
  query = std::move(staged); offset = parsed;
  return true;
}
}
