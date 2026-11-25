#include "input_profiles.hpp"
#include "cJSON.h"
#include "content_paths.hpp"
#include <cstring>
#include "file_read.hpp"
#include <memory>
#include <set>

namespace tab5::input {
Bindings defaults(Profile p) {
  Bindings out{{18, 14, 12, 13, 28, 23, 8, 22, 4, 7, 24, 19, 0, 0, 41}};
  if (p == Profile::GameBoy || p == Profile::Gba)
    out.keys[2] = out.keys[3] = 0;
  if (p == Profile::GameBoy)
    out.keys[10] = out.keys[11] = 0;
  if (p == Profile::MegaDrive)
    out.keys = {14, 13, 0, 18, 0, 23, 8, 22, 4, 7, 0, 0, 0, 0, 41};
  if (p == Profile::MegaDrive6)
    out.keys = {14, 13, 12, 18, 28, 23, 8, 22, 4, 7, 24, 19, 0, 0, 41};
  if(p==Profile::N64)out.keys[12]=17,out.keys[13]=16; // N/M: C-down/right
  return out;
}
GamepadState map(const uint8_t (&keys)[32], const Bindings &bindings) {
  GamepadState out;
  for (unsigned i = 0; i < 14; ++i) {
    const auto u = bindings.keys[i];
    if (u && (keys[u >> 3] & (1u << (u & 7))))
      out.buttons |= 1u << i;
  }
  // Arrows remain navigation aliases; EASD is the default physical layout.
  out.up |= !!(keys[82 >> 3] & (1u << (82 & 7)));
  out.down |= !!(keys[81 >> 3] & (1u << (81 & 7)));
  out.left |= !!(keys[80 >> 3] & (1u << (80 & 7)));
  out.right |= !!(keys[79 >> 3] & (1u << (79 & 7)));
  if (out.up && out.down)
    out.up = out.down = 0;
  if (out.left && out.right)
    out.left = out.right = 0;
  return out;
}
bool menu_pressed(const uint8_t (&keys)[32], const Bindings &bindings) {
  // Escape is a permanent recovery route even when the menu is remapped.
  const auto u = bindings.keys[14];
  return (keys[41 >> 3] & (1u << (41 & 7))) ||
         (u && (keys[u >> 3] & (1u << (u & 7))));
}
namespace {
constexpr size_t max_bytes = 65536;
bool bounded(std::string_view text) {
  if (text.empty() || text.size() > max_bytes || text.find('\0') != text.npos ||
      text.find("\\u0000") != text.npos)
    return false;
  int depth = 0;
  unsigned tokens = 0;
  bool quoted = false, escaped = false;
  for (unsigned char c : text) {
    if (c >= 128)
      return false; // This file contains only ASCII keys/identities.
    if (quoted) {
      if (escaped)
        escaped = false;
      else if (c == '\\')
        escaped = true;
      else if (c == '"')
        quoted = false;
    } else if (c == '"')
      quoted = true;
    else if (c == '{' || c == '[') {
      if (++depth > 6 || ++tokens > 4096)
        return false;
    } else if (c == '}' || c == ']') {
      if (--depth < 0)
        return false;
    } else if ((c == ',' || c == ':') && ++tokens > 4096)
      return false;
  }
  return depth == 0 && !quoted;
}
bool unique_fields(const cJSON *node) {
  if (cJSON_IsObject(node)) {
    std::set<std::string> names;
    for (auto *field = node->child; field; field = field->next)
      if (!field->string || !names.insert(field->string).second ||
          !unique_fields(field))
        return false;
  } else if (cJSON_IsArray(node)) {
    for (auto *item = node->child; item; item = item->next)
      if (!unique_fields(item))
        return false;
  }
  return true;
}
int usage(const cJSON *node) {
  return cJSON_IsString(node) && node->valuestring
             ? key_usage(node->valuestring)
             : -1;
}
int action(std::string_view name, Profile p) {
  if(p==Profile::N64){
    if(name=="c_up")return 2;
    if(name=="c_left")return 3;
    if(name=="z")return 4;
    if(name=="c_down")return 12;
    if(name=="c_right")return 13;
  }
  if (p == Profile::MegaDrive || p == Profile::MegaDrive6) {
    if (name == "a")
      return 1;
    if (name == "b")
      return 0;
    if (name == "c")
      return 3;
    if (name == "y")
      return 10;
    if (name == "z")
      return 11;
    if (name == "mode")
      return 4;
  }
  constexpr std::string_view names[] = {"a",     "b",  "x",    "y",    "select",
                                        "start", "up", "down", "left", "right",
                                        "l",     "r",  "l2",   "r2",   "menu"};
  for (unsigned i = 0; i < std::size(names); ++i)
    if (name == names[i])
      return i;
  return -1;
}
bool layer(const cJSON *object, Profile p, Bindings &out, bool global = false) {
  if (!object)
    return true;
  if (!cJSON_IsObject(object))
    return false;
  const auto base = defaults(p);
  for (auto *node = object->child; node; node = node->next) {
    const int slot = action(node->string, p), key = usage(node);
    if (slot < 0 || key < 0 || (key == 41 && slot != 14))
      return false;
    if (!base.keys[slot]) {
      if (global || (p == Profile::MegaDrive &&
                     (slot == 2 || slot == 4 || slot == 10 || slot == 11)))
        continue;
      return false;
    }
    out.keys[slot] = key;
  }
  return true;
}
bool conflict_free(const Bindings &out) {
  std::array<bool, 256> used{};
  for (auto key : out.keys)
    if (key) {
      if (used[key])
        return false;
      used[key] = true;
    }
  constexpr uint8_t arrows[] = {82, 81, 80, 79};
  for (unsigned i = 0; i < out.keys.size(); ++i)
    for (unsigned d = 0; d < 4; ++d)
      if (out.keys[i] == arrows[d] && i != 6 + d)
        return false;
  return true;
}
} // namespace
ProfileSelection parse_profile(std::string_view json, Profile p,
                               std::string_view system, std::string_view hash) {
  system = profile_system(system);
  ProfileSelection result{.bindings = defaults(p),
                          .valid = false,
                          .from_file = true,
                          .error = "perfil invalido; usando padroes"};
  if (!bounded(json))
    return result;
  const std::string text(json);
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_ParseWithOpts(text.c_str(), nullptr, true), cJSON_Delete);
  if (!cJSON_IsObject(root.get()) || !unique_fields(root.get()))
    return result;
  auto get = [](const cJSON *obj, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(obj, key);
  };
  const auto *version = get(root.get(), "schema_version");
  if (!cJSON_IsNumber(version) || version->valuedouble != 1)
    return result;
  Bindings chosen = defaults(p);
  if (!layer(get(root.get(), "global"), p, chosen, true))
    return result;
  const auto *systems = get(root.get(), "systems");
  const auto *games = get(root.get(), "games");
  if ((systems && !cJSON_IsObject(systems)) ||
      (games && !cJSON_IsObject(games)))
    return result;
  // Read the original flat schema too: never overwrite a user's file to
  // migrate.
  auto *console =
      get(systems ? systems : root.get(), std::string(system).c_str());
  if (!console && system == "gbc")
    console = get(systems ? systems : root.get(), "gb");
  if (!layer(console, p, chosen))
    return result;
  if (!hash.empty() && !layer(get(games, std::string(hash).c_str()), p, chosen))
    return result;
  if (!conflict_free(chosen)) {
    result.error = "conflito de teclas; usando padroes";
    return result;
  }
  result.bindings = chosen;
  result.valid = true;
  result.error.clear();
  return result;
}
ProfileSelection load_profile(Profile p, std::string_view system,
                              std::string_view hash) {
  content::FileBytes bytes;
  const auto read=content::read_file(content::resolve("config/input_profiles.json"),max_bytes,bytes);
  if (read == content::ReadStatus::Missing)
    return {.bindings=defaults(p), .valid=true, .from_file=false, .error={}};
  if (read != content::ReadStatus::Ok || !bytes.size())
    return {.bindings=defaults(p), .valid=false, .from_file=true,
            .error="leitura ou tamanho de perfil invalido; usando padroes"};
  return parse_profile(std::string_view(reinterpret_cast<const char *>(bytes.data()),bytes.size()),p,system,hash);
}

std::string key_name(uint8_t code) {
  if (!code)
    return "NONE";
  if (code >= 4 && code <= 29)
    return std::string(1, char('A' + code - 4));
  if (code >= 30 && code <= 38)
    return std::string(1, char('1' + code - 30));
  if (code == 39)
    return "0";
  if (code >= 58 && code <= 69)
    return "F" + std::to_string(code - 57);
  switch (code) {
  case 40:
    return "ENTER";
  case 41:
    return "ESC";
  case 44:
    return "SPACE";
  case 82:
    return "UP";
  case 81:
    return "DOWN";
  case 80:
    return "LEFT";
  case 79:
    return "RIGHT";
  }
  return {};
}
int key_usage(std::string_view name) {
  if (name == "NONE")
    return 0;
  if (name.size() == 1) {
    const char c = name[0];
    if (c >= 'A' && c <= 'Z')
      return c - 'A' + 4;
    if (c >= '1' && c <= '9')
      return c - '1' + 30;
    if (c == '0')
      return 39;
  }
  constexpr std::pair<std::string_view, int> special[] = {
      {"ENTER", 40}, {"ESC", 41},   {"SPACE", 44}, {"UP", 82}, {"DOWN", 81},
      {"LEFT", 80},  {"RIGHT", 79}, {"F1", 58},    {"F2", 59}, {"F3", 60},
      {"F4", 61},    {"F5", 62},    {"F6", 63},    {"F7", 64}, {"F8", 65},
      {"F9", 66},    {"F10", 67},   {"F11", 68},   {"F12", 69}};
  for (auto [label, code] : special)
    if (name == label)
      return code;
  return -1;
}

std::string action_name(Profile p, unsigned slot) {
  static constexpr const char *names[] = {
      "a",    "b",     "x", "y", "select", "start", "up",  "down",
      "left", "right", "l", "r", "l2",     "r2",    "menu"};
  if (slot >= 15)
    return {};
  if(p==Profile::N64){
    if(slot==2)return "c_up";
    if(slot==3)return "c_left";
    if(slot==4)return "z";
    if(slot==12)return "c_down";
    if(slot==13)return "c_right";
  }
  if (p == Profile::MegaDrive || p == Profile::MegaDrive6) {
    switch (slot) {
    case 0:
      return "b";
    case 1:
      return "a";
    case 3:
      return "c";
    case 4:
      return "mode";
    case 10:
      return "y";
    case 11:
      return "z";
    }
  }
  return names[slot];
}
EditResult edit_profile(std::string_view previous, Profile p,
                        std::string_view system, std::string_view hash,
                        Scope scope, const Bindings &bindings, uint16_t changed,
                        bool reset) {
  system = profile_system(system);
  EditResult result;
  result.error = "Perfil invalido; arquivo preservado";
  if (scope == Scope::Game &&
      (hash.size() != 64 ||
       hash.find_first_not_of("0123456789abcdef") != hash.npos)) {
    result.error = "Identidade do jogo ainda indisponivel";
    return result;
  }
  if (previous.empty())
    previous = R"({"schema_version":1,"global":{},"systems":{},"games":{}})";
  const auto old = parse_profile(previous, p, system, hash);
  if (!old.valid)
    return result;
  const std::string source(previous);
  std::unique_ptr<cJSON, decltype(&cJSON_Delete)> root(
      cJSON_Parse(source.c_str()), cJSON_Delete);
  auto get = [](cJSON *n, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(n, key);
  };
  auto object = [&](cJSON *parent, const char *key) {
    auto *v = get(parent, key);
    if (!v) {
      v = cJSON_CreateObject();
      cJSON_AddItemToObject(parent, key, v);
    }
    return v;
  };
  cJSON *parent = root.get();
  std::string target = "global";
  if (scope == Scope::System) {
    const bool legacy = !get(root.get(), "systems");
    parent = object(root.get(), "systems");
    if (legacy)
      for (auto key : {"gb", "gbc", "gba", "snes", "megadrive"})
        if (auto *v = get(root.get(), key))
          cJSON_AddItemToObject(parent, key, cJSON_Duplicate(v, true));
    target = system;
  } else if (scope == Scope::Game) {
    parent = object(root.get(), "games");
    target = hash;
  }
  if (!cJSON_IsObject(parent))
    return result;
  if (reset)
    cJSON_DeleteItemFromObjectCaseSensitive(parent, target.c_str());
  auto *layer = object(parent, target.c_str());
  if (!cJSON_IsObject(layer))
    return result;
  const auto base = defaults(p);
  for (unsigned slot = 0; slot < 15; ++slot)
    if (changed & (1u << slot)) {
      const auto key = key_name(bindings.keys[slot]);
      if (!base.keys[slot] || key.empty())
        return result;
      const auto name = action_name(p, slot);
      cJSON_DeleteItemFromObjectCaseSensitive(layer, name.c_str());
      cJSON_AddStringToObject(layer, name.c_str(), key.c_str());
    }
  char *raw = cJSON_Print(root.get());
  if (!raw)
    return result;
  result.text = std::string(raw) + "\n";
  cJSON_free(raw);
  const auto selected =
      parse_profile(result.text, p, scope == Scope::Global ? "" : system,
                    scope == Scope::Game ? hash : "");
  if (!selected.valid) {
    result.text.clear();
    result.error = selected.error;
    return result;
  }
  if (scope == Scope::Global) {
    for (auto context :
         {std::pair{Profile::Snes, "snes"}, std::pair{Profile::GameBoy, "gb"},
          std::pair{Profile::GameBoy, "gbc"}, std::pair{Profile::Gba, "gba"},
          std::pair{Profile::MegaDrive, "megadrive"},
          std::pair{Profile::MegaDrive6, "megadrive"},std::pair{Profile::N64,"n64"}}) {
      if (!parse_profile(result.text, context.first, context.second).valid) {
        result.text.clear();
        result.error = "Conflito em outro sistema; arquivo preservado";
        return result;
      }
    }
  }
  result.bindings = selected.bindings;
  result.valid = true;
  result.error.clear();
  return result;
}
} // namespace tab5::input
