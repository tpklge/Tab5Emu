#pragma once
#include <string>
#include <string_view>

namespace tab5::content {
#ifdef TAB5_CONTENT_ROOT
inline constexpr char root[] = TAB5_CONTENT_ROOT;
#else
inline constexpr char root[] = "/sdcard/Tab5-Emu";
#endif
inline bool valid_relative(std::string_view path) {
  if (path.empty() || path.size() > 240) return false;
  for (unsigned char c : path)
    if (c < 32 || c == '\\' || c == ':' || c == 127) return false;
  size_t start = 0;
  while (start <= path.size()) {
    auto end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    auto part = path.substr(start, end - start);
    if (part.empty() || part == "." || part == "..") return false;
    if (end == path.size()) break;
    start = end + 1;
  }
  return true;
}
inline std::string resolve(std::string_view relative) {
  return valid_relative(relative) ? std::string(root) + "/" + std::string(relative) : std::string();
}
} // namespace tab5::content
