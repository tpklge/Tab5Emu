#pragma once
#include <cstdint>
#include <map>
#include <string>

/// The launcher settings that survive a power cycle, version 1. Kept in
/// settings.bin at the root of the SD card by espp::PersistentData: a new
/// field means a new version struct (with a migrate() from this one) in
/// front of the type list in AppSettings, never a change to this one.
struct SettingsV1 {
  static constexpr uint32_t VERSION = 1;
  uint8_t volume{60}; ///< 0..100
  bool muted{false};
  uint8_t brightness{80};      ///< 0..100
  uint8_t video_setting{1};    ///< VideoSetting (0 original, 1 fit, 2 fill)
  bool resume_last_save{true}; ///< starting a game loads the slot it was last
                               ///< saved to / loaded from
  std::string
      last_rom; ///< rom_path of the last game started (selected again at boot)
  std::map<std::string, uint8_t>
      last_slot; ///< rom_path -> the save slot last saved to / loaded from
};
