#pragma once
#include "settings_data.hpp"
#include <string_view>
#include <vector>
struct UiSettings {
  std::string theme{"system"}, search;
  bool idle_in_game{false}, md_six_buttons{false};
  int gba_cpu{0};
  int library_view{0}, idle_seconds{0};
  std::vector<std::string> favorites, recent;
  bool operator==(const UiSettings &) const = default;
};
// Optional fields merge with legacy binary settings, transactionally.
bool parse_settings_json(std::string_view text, SettingsV1 &base,
                         UiSettings &ui);
// Preserve unknown fields from the last valid file.
std::string encode_settings_json(std::string_view previous,
                                 const SettingsV1 &base, const UiSettings &ui);
struct ThemeConfig { int mode{0}, strength{100}; };
bool parse_theme_json(std::string_view text, ThemeConfig &out);
