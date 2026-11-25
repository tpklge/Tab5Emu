#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "persistent_data.hpp"

#include "settings_data.hpp"
#include "settings_json.hpp"

/// The current settings version.
using Settings = SettingsV1;

/// The settings file and an in-RAM copy of it. Changes are made in RAM
/// (modify()) and written when save() is called; the launcher calls save()
/// a moment after the last change, so dragging a slider is one write.
/// Thread-safe.
class AppSettings {
public:
  static AppSettings &get() {
    static AppSettings instance;
    return instance;
  }

  /// Load the file, or create it from `defaults` when there is none. A file
  /// that cannot be read (corrupt, or written by newer firmware) is reported
  /// and replaced by `defaults` in RAM; the file itself is only overwritten by
  /// the next save().
  bool init(const std::string &file_path, const Settings &defaults);

  /// A copy of the current settings.
  Settings get_settings() const;

  /// Change the settings in RAM (written by the next save()).
  template <typename Fn> void modify(Fn &&fn) {
    std::lock_guard<std::mutex> lk(mutex_);
    Settings s = data_ ? data_->get() : fallback_;
    const Settings before = s;
    fn(s);
    if (!same(before, s)) {
      if (data_)
        data_->set(s);
      else
        fallback_ = s;
      dirty_ = true;
      last_change_ = std::chrono::steady_clock::now();
    }
  }

  UiSettings ui_settings() const { std::lock_guard<std::mutex> lk(mutex_);return ui_; }
  template <typename Fn> void modify_ui(Fn &&fn) {
    std::lock_guard<std::mutex> lk(mutex_);const auto before=ui_;fn(ui_);
    if(ui_!=before){dirty_=true;last_change_=std::chrono::steady_clock::now();}
  }
  /// Write the settings if they changed since the last save.
  bool save();

  /// save(), but only once `quiet` has passed since the last change (call it
  /// periodically: a burst of changes is written once, after it ends).
  bool save_if_idle(std::chrono::milliseconds quiet = std::chrono::milliseconds(1500));

  /// The save slot last used for `rom_path`, if any.
  std::optional<int> last_slot(const std::string &rom_path) const;
  /// Remember `slot` as the last one used for `rom_path`.
  void set_last_slot(const std::string &rom_path, int slot);

protected:
  AppSettings() = default;
  static bool same(const Settings &a, const Settings &b);

  using Store = espp::PersistentData<Settings>;
  mutable std::mutex mutex_;
  std::unique_ptr<Store> data_;
  Settings fallback_{}; ///< used when init() was not called (no SD card)
  UiSettings ui_{};
  std::string json_path_,json_base_;
  bool json_valid_{true};
  bool dirty_{false};
  std::chrono::steady_clock::time_point last_change_{};
};
