#include "rom_info.hpp"
#include "file_read.hpp"
#include "content_paths.hpp"
#include "cJSON.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <set>

namespace {
constexpr size_t max_bytes = 1024 * 1024, max_games = 2048;
struct System { const char *key; Emulator platform; const char *extensions; };
constexpr System systems[] = {
  {"snes", Emulator::SNES, ".sfc|.smc|"}, {"gb", Emulator::GAMEBOY, ".gb|"},
  {"gbc", Emulator::GAMEBOY_COLOR, ".gbc|"}, {"gba", Emulator::GBA, ".gba|"},
  {"megadrive", Emulator::SEGA_MEGA_DRIVE, ".gen|.md|.smd|"}
#if defined(ENABLE_N64) || !defined(ESP_PLATFORM)
  ,{"n64", Emulator::N64, ".z64|.n64|.v64|"}
#endif
};
bool utf8(const std::string &s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t c = static_cast<unsigned char>(s[i++]);
    if (c < 128) { if (c == 0) return false; continue; }
    unsigned n; uint32_t minimum;
    if (c >= 0xc2 && c <= 0xdf) { c &= 31; n = 1; minimum = 128; }
    else if (c >= 0xe0 && c <= 0xef) { c &= 15; n = 2; minimum = 2048; }
    else if (c >= 0xf0 && c <= 0xf4) { c &= 7; n = 3; minimum = 65536; }
    else return false;
    if (i + n > s.size()) return false;
    for (unsigned j = 0; j < n; ++j) {
      auto next = static_cast<unsigned char>(s[i++]);
      if ((next & 0xc0) != 0x80) return false;
      c = (c << 6) | (next & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) return false;
  }
  return true;
}
// Bound parser recursion before cJSON allocates its tree.
bool bounded_json(const std::string &s) {
  int depth = 0; bool quoted = false, escaped = false;
  size_t tokens = 0;
  for (char c : s) {
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') quoted = true;
    else if (c == '[' || c == '{') { if (++depth > 8 || ++tokens > 32768) return false; }
    else if (c == ']' || c == '}') { if (--depth < 0) return false; }
    else if ((c == ',' || c == ':') && ++tokens > 32768) return false;
  }
  return !quoted && depth == 0;
}
const char *field(const cJSON *object, const char *key) {
  auto *value = cJSON_GetObjectItemCaseSensitive(object, key);
  return cJSON_IsString(value) ? value->valuestring : nullptr;
}
bool extension(const std::string &path, const System &system) {
  auto ext = std::filesystem::path(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
  return !ext.empty() && std::string(system.extensions).find(ext + "|") != std::string::npos;
}
bool identity(const std::string &s) {
  if (s.empty() || s.size() > 80) return false;
  for (unsigned char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}
}

std::vector<RomInfo> load_library() {
  namespace fs = std::filesystem;
  std::vector<RomInfo> out;
  std::set<std::string> paths, ids;
  tab5::content::FileBytes bytes;
  if (tab5::content::read_file(tab5::content::resolve("library/games.json"), max_bytes, bytes) == tab5::content::ReadStatus::Ok) {
    if (bytes.size()) {
      std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
      // cJSON supports escaped NUL; reject it to avoid truncating path fields.
      if (utf8(text) && bounded_json(text) && text.find("\\u0000") == std::string::npos) {
        std::unique_ptr<cJSON, decltype(&cJSON_Delete)> json(cJSON_ParseWithOpts(text.c_str(), nullptr, true), cJSON_Delete);
        auto *version = cJSON_GetObjectItemCaseSensitive(json.get(), "schema_version");
        auto *games = cJSON_GetObjectItemCaseSensitive(json.get(), "games");
        if (cJSON_IsNumber(version) && version->valuedouble == 1 && cJSON_IsArray(games) && cJSON_GetArraySize(games) <= static_cast<int>(max_games)) {
          const cJSON *game;
          cJSON_ArrayForEach(game, games) {
            const char *id = field(game, "id"), *system = field(game, "system"), *title = field(game, "title"), *rom = field(game, "rom");
            const char *cover = field(game, "cover"), *description = field(game, "description");
            if (!id || !identity(id) || !ids.insert(id).second || !system || !title || !rom ||
                !*title || std::strlen(title) > 160 || !tab5::content::valid_relative(rom) ||
                (description && std::strlen(description) > 2048)) continue;
            for (const auto &s : systems) {
              if (std::string(system) != s.key || !extension(rom, s)) continue;
              const auto path = tab5::content::resolve(rom);
              std::error_code ec;
              if (!fs::is_regular_file(path, ec) || !paths.insert(path).second) break;
              std::string art;
              if (cover && tab5::content::valid_relative(cover)) {
                auto candidate = tab5::content::resolve(cover);
                auto ext = fs::path(candidate).extension().string();
                if ((ext == ".jpg" || ext == ".jpeg") && fs::is_regular_file(candidate, ec) && fs::file_size(candidate, ec) <= 256 * 1024)
                  art = candidate;
              }
              out.push_back({title, art, path, s.platform, description ? description : "", id});
              break;
            }
          }
        } else fmt::print("library: catalog version/shape/limit rejected; scanning ROMs\n");
      } else fmt::print("library: invalid UTF-8/JSON depth/NUL; scanning ROMs\n");
    } else fmt::print("library: catalog exceeds 1 MiB or empty; scanning ROMs\n");
  }
  // Missing or invalid per-game metadata never prevents raw ROM discovery.
  for (const auto &system : systems) {
    std::error_code ec;
    fs::recursive_directory_iterator it(tab5::content::resolve(std::string("roms/") + system.key), fs::directory_options::skip_permission_denied, ec), end;
    size_t visited = 0;
    while (!ec && it != end && out.size() < max_games && ++visited <= 16384) {
      if (it.depth() >= 4) it.disable_recursion_pending();
      auto path = it->path();
      const auto name=path.filename().string();
      if(!name.empty() && name[0]=='.') { it.disable_recursion_pending(); it.increment(ec); continue; }
      if (it->is_regular_file(ec) && extension(path.string(), system) && paths.insert(path.string()).second)
        out.push_back({path.stem().string(), "", path.string(), system.platform});
      it.increment(ec);
    }
  }
  fmt::print("library: {} games (catalog + discovery)\n", out.size());
  return out;
}
