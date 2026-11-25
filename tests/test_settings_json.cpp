#include <vector>
#include "settings_json.hpp"
#include <cassert>
#include <iostream>
int main() {
  {
    ThemeConfig theme;
    assert(parse_theme_json(R"({"schema_version":1,"mode":"invert","strength":75})",theme));
    assert(theme.mode==2 && theme.strength==75);
    for (auto text : {R"({"schema_version":2,"mode":"console"})",
                      R"({"schema_version":1,"mode":"unknown"})",
                      R"({"schema_version":1,"mode":"console","strength":100.5})",
                      R"({"schema_version":1,"mode":"console","strength":-1})",
                      R"({"schema_version":1,"mode":"console","mode":"invert"})",
                      R"({"schema_version":1,"mode":3})"}) {
      assert(!parse_theme_json(text,theme));
      assert(theme.mode==2 && theme.strength==75);
    }
  }
  SettingsV1 base;
  base.volume = 27;
  base.last_rom = "kept";
  UiSettings ui;
  assert(parse_settings_json(
      R"({"schema_version":1,"volume":42,"muted":true,"ui":{"search":"ação","favorites":["/sdcard/roms/a.gb"],"idle_seconds":15}})",
      base, ui));
  assert(base.volume == 42 && base.muted && base.last_rom == "kept" &&
         ui.search == "ação" && ui.favorites.size() == 1 &&
         ui.idle_seconds == 15);
  for (auto invalid :
       {R"({"schema_version":2})", R"({"schema_version":1,"volume":101})",
        R"({"schema_version":1,"volume":3,"volume":4})",
        R"({"schema_version":1,"ui":{"favorites":["a","a"]}})",
        R"({"schema_version":1,"ui":{"idle_seconds":2}})",
        R"({"schema_version":1,"muted":1})",
        R"({"schema_version":1,"ui":{"theme":"../../x"}})",
        R"({"schema_version":1,"volume":1.5})"}) {
    const auto before = base;
    const auto ui_before = ui;
    assert(!parse_settings_json(invalid, base, ui));
    assert(base.volume == before.volume && base.muted == before.muted &&
           ui == ui_before);
  }
  auto encoded = encode_settings_json(
      R"({"schema_version":1,"future":{"preserve":true},"ui":{"unknown":"kept"}})",
      base, ui);
  assert(encoded.find("preserve") != std::string::npos &&
         encoded.find("unknown") != std::string::npos);
  SettingsV1 restored;
  UiSettings restored_ui;
  assert(parse_settings_json(encoded, restored, restored_ui));
  assert(restored.volume == base.volume && restored_ui == ui);
  assert(!parse_settings_json(std::string(65537, ' '), base, ui));
  assert(!parse_settings_json(
      R"({"schema_version":1,"ui":{"search":"a\u0000b"}})", base, ui));
  // Persist long absolute paths without truncation or losing preferences.
  ui.favorites = {
      "/sdcard/Tab5-Emu/" +
      std::string(257 - std::string("/sdcard/Tab5-Emu/").size(), 'a')};
  assert(ui.favorites[0].size() == 257);
  encoded = encode_settings_json("", base, ui);
  assert(!encoded.empty() &&
         parse_settings_json(encoded, restored, restored_ui));
  assert(restored_ui.favorites == ui.favorites);
  ui.favorites = {std::string(321, 'a')};
  assert(encode_settings_json("", base, ui).empty());
  ui.favorites.clear();
  for (const std::string &bad :
       {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
        std::string("\xf4\x90\x80\x80"), std::string("\xe2\x82"),
        std::string("\x80"), std::string("\xc2x")}) {
    const auto previous_ui = ui;
    assert(!parse_settings_json("{\"schema_version\":1,\"ui\":{\"search\":\"" +
                                    bad + "\"}}",
                                base, ui));
    assert(ui == previous_ui);
  }
  // Preserve valid unknown fields, but refuse UI growth beyond the file limit.
  const std::string large =
      "{\"schema_version\":1,\"future\":\"" + std::string(65000, 'a') + "\"}";
  assert(parse_settings_json(large, base, ui));
  assert(!encode_settings_json(large, base, ui).empty());
  ui.favorites =
      std::vector<std::string>{std::string(320, 'b'), std::string(320, 'c')};
  assert(encode_settings_json(large, base, ui).empty());
  ui.favorites.clear();
  const std::string prefix = "{\"schema_version\":1,\"future\":\"";
  const std::string suffix = "\"}";
  const auto overhead = encode_settings_json(prefix + suffix, base, ui).size();
  const auto at_limit = prefix + std::string(65536 - overhead, 'x') + suffix;
  assert(encode_settings_json(at_limit, base, ui).size() == 65536);
  const auto beyond_limit =
      prefix + std::string(65537 - overhead, 'x') + suffix;
  assert(parse_settings_json(beyond_limit, base, ui));
  assert(encode_settings_json(beyond_limit, base, ui).empty());
  std::cout << "settings JSON: transactional validation, bounded preferences, "
               "unknown fields, legacy merge and roundtrip OK\n";
}
