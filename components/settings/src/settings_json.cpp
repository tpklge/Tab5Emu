#include "settings_json.hpp"
#include "cJSON.h"
#include <cmath>
#include <cstring>
#include <memory>
#include <set>
namespace {
using Json = std::unique_ptr<cJSON, decltype(&cJSON_Delete)>;
bool utf8(std::string_view s) {
  for (size_t i = 0; i < s.size();) {
    uint32_t c = static_cast<unsigned char>(s[i++]);
    if (c < 128) {
      if (c == 0)
        return false;
      continue;
    }
    unsigned n;
    uint32_t minimum;
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
    for (unsigned j = 0; j < n; ++j) {
      auto next = static_cast<unsigned char>(s[i++]);
      if ((next & 0xc0) != 0x80)
        return false;
      c = (c << 6) | (next & 63);
    }
    if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
      return false;
  }
  return true;
}
bool bounded(std::string_view text) {
  if (text.empty() || text.size() > 65536 || text.find('\0') != text.npos ||
      text.find("\\u0000") != text.npos || !utf8(text))
    return false;
  unsigned tokens = 0;
  int depth = 0;
  bool string = false, escape = false;
  for (unsigned char c : text) {
    if (string) {
      if (escape)
        escape = false;
      else if (c == '\\')
        escape = true;
      else if (c == '"')
        string = false;
    } else if (c == '"')
      string = true;
    else if (c == '{' || c == '[') {
      if (++depth > 6 || ++tokens > 4096)
        return false;
    } else if (c == '}' || c == ']') {
      if (--depth < 0)
        return false;
    } else if ((c == ',' || c == ':') && ++tokens > 4096)
      return false;
  }
  return !string && depth == 0;
}
bool unique(const cJSON *n) {
  std::set<std::string> fields;
  for (auto *c = n->child; c; c = c->next) {
    if (cJSON_IsObject(n) && (!c->string || !fields.insert(c->string).second))
      return false;
    if (!unique(c))
      return false;
  }
  return true;
}
bool number(const cJSON *root, const char *key, int &out, int low, int high) {
  const auto *v = cJSON_GetObjectItemCaseSensitive(root, key);
  if (!v)
    return true;
  if (!cJSON_IsNumber(v) || !std::isfinite(v->valuedouble) ||
      v->valuedouble < low || v->valuedouble > high ||
      std::floor(v->valuedouble) != v->valuedouble)
    return false;
  out = v->valueint;
  return true;
}
bool flag(const cJSON *r, const char *key, bool &out) {
  const auto *v = cJSON_GetObjectItemCaseSensitive(r, key);
  if (!v)
    return true;
  if (!cJSON_IsBool(v))
    return false;
  out = cJSON_IsTrue(v);
  return true;
}
bool text(const cJSON *r, const char *key, std::string &out, size_t max) {
  const auto *v = cJSON_GetObjectItemCaseSensitive(r, key);
  if (!v)
    return true;
  if (!cJSON_IsString(v) || !v->valuestring || strlen(v->valuestring) > max)
    return false;
  out = v->valuestring;
  return true;
}
bool list(const cJSON *r, const char *key, std::vector<std::string> &out,
          size_t maximum) {
  const auto *v = cJSON_GetObjectItemCaseSensitive(r, key);
  if (!v)
    return true;
  if (!cJSON_IsArray(v) || cJSON_GetArraySize(v) > int(maximum))
    return false;
  std::vector<std::string> staged;
  std::set<std::string> seen;
  for (const auto *item = v->child; item; item = item->next) {
    if (!cJSON_IsString(item) || !item->valuestring || !*item->valuestring ||
        strlen(item->valuestring) > 320 ||
        !seen.insert(item->valuestring).second)
      return false;
    staged.emplace_back(item->valuestring);
  }
  out = std::move(staged);
  return true;
}
void put(cJSON *r, const char *key, cJSON *v) {
  cJSON_DeleteItemFromObjectCaseSensitive(r, key);
  cJSON_AddItemToObject(r, key, v);
}
cJSON *array(const std::vector<std::string> &values) {
  auto *a = cJSON_CreateArray();
  for (const auto &v : values)
    cJSON_AddItemToArray(a, cJSON_CreateString(v.c_str()));
  return a;
}
} // namespace
bool parse_settings_json(std::string_view bytes, SettingsV1 &base,
                         UiSettings &ui) {
  if (!bounded(bytes))
    return false;
  const std::string input(bytes);
  Json root(cJSON_ParseWithOpts(input.c_str(), nullptr, true), cJSON_Delete);
  if (!root || !cJSON_IsObject(root.get()) || !unique(root.get()))
    return false;
  int schema = 0;
  if (!number(root.get(), "schema_version", schema, 1, 1) || schema != 1)
    return false;
  auto b = base;
  auto u = ui;
  int volume = b.volume, brightness = b.brightness, video = b.video_setting;
  if (!number(root.get(), "volume", volume, 0, 100) ||
      !number(root.get(), "brightness", brightness, 5, 100) ||
      !number(root.get(), "video", video, 0, 2) ||
      !flag(root.get(), "muted", b.muted) ||
      !flag(root.get(), "resume_last_save", b.resume_last_save))
    return false;
  b.volume = volume;
  b.brightness = brightness;
  b.video_setting = video;
  const auto *node = cJSON_GetObjectItemCaseSensitive(root.get(), "ui");
  if (node) {
    if (!cJSON_IsObject(node) || !flag(node, "idle_in_game", u.idle_in_game) ||
        !flag(node, "md_six_buttons", u.md_six_buttons) ||
        !number(node, "gba_cpu", u.gba_cpu, 0, 1) ||
        !text(node, "theme", u.theme, 16) ||
        !text(node, "search", u.search, 128) ||
        !number(node, "library_view", u.library_view, 0, 2) ||
        !number(node, "idle_seconds", u.idle_seconds, 0, 3600) ||
        !list(node, "favorites", u.favorites, 128) ||
        !list(node, "recent", u.recent, 20))
      return false;
    if (u.theme != "system" && u.theme != "dark" && u.theme != "light")
      return false;
    if (u.idle_seconds && u.idle_seconds < 15)
      return false;
  }
  base = std::move(b);
  ui = std::move(u);
  return true;
}
bool parse_theme_json(std::string_view text, ThemeConfig &out) {
  if (text.size() > 1024) return false;
  SettingsV1 base; UiSettings ui;
  if (!parse_settings_json(text, base, ui)) return false;
  const std::string bytes(text);
  Json root(cJSON_Parse(bytes.c_str()), cJSON_Delete);
  const auto *mode = cJSON_GetObjectItemCaseSensitive(root.get(), "mode");
  if (!cJSON_IsString(mode)) return false;
  ThemeConfig staged;
  if (!std::strcmp(mode->valuestring,"console")) staged.mode=0;
  else if (!std::strcmp(mode->valuestring,"grayscale")) staged.mode=1;
  else if (!std::strcmp(mode->valuestring,"invert")) staged.mode=2;
  else return false;
  if (!number(root.get(), "strength", staged.strength, 0, 100)) return false;
  out=staged; return true;
}
std::string encode_settings_json(std::string_view previous,
                                 const SettingsV1 &base, const UiSettings &ui) {
  std::string input(previous);
  Json root(bounded(input) ? cJSON_Parse(input.c_str()) : nullptr,
            cJSON_Delete);
  if (!root || !cJSON_IsObject(root.get()))
    root.reset(cJSON_CreateObject());
  put(root.get(), "schema_version", cJSON_CreateNumber(1));
  put(root.get(), "volume", cJSON_CreateNumber(base.volume));
  put(root.get(), "brightness", cJSON_CreateNumber(base.brightness));
  put(root.get(), "video", cJSON_CreateNumber(base.video_setting));
  put(root.get(), "muted", cJSON_CreateBool(base.muted));
  put(root.get(), "resume_last_save", cJSON_CreateBool(base.resume_last_save));
  auto *node = cJSON_GetObjectItemCaseSensitive(root.get(), "ui");
  if (!cJSON_IsObject(node)) {
    put(root.get(), "ui", cJSON_CreateObject());
    node = cJSON_GetObjectItemCaseSensitive(root.get(), "ui");
  }
  put(node, "gba_cpu", cJSON_CreateNumber(ui.gba_cpu));
  put(node, "md_six_buttons", cJSON_CreateBool(ui.md_six_buttons));
  put(node, "idle_in_game", cJSON_CreateBool(ui.idle_in_game));
  put(node, "theme", cJSON_CreateString(ui.theme.c_str()));
  put(node, "search", cJSON_CreateString(ui.search.c_str()));
  put(node, "library_view", cJSON_CreateNumber(ui.library_view));
  put(node, "idle_seconds", cJSON_CreateNumber(ui.idle_seconds));
  put(node, "favorites", array(ui.favorites));
  put(node, "recent", array(ui.recent));
  char *raw = cJSON_Print(root.get());
  if (!raw)
    return {};
  std::string result(raw);
  cJSON_free(raw);
  result += "\n";
  SettingsV1 verify;
  UiSettings verify_ui;
  if (!parse_settings_json(result, verify, verify_ui))
    return {};
  return result;
}
