#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "lvgl.h"

#include "event_manager.hpp"
#include "high_resolution_timer.hpp"
#include "logger.hpp"

#include "emu-ui/launcher.hpp"

#include "content_paths.hpp"
#include "controls_editor.hpp"
#include "library_index.hpp"
#include "rom_info.hpp"
#include "settings.hpp"
#include "tab5-emu.hpp"

/// The launcher: the games in metadata.csv on the SD card, grouped by
/// console, with the selected game's boxart, a console picker and the
/// settings (kept in AppSettings). The screens are emu-ui's (shared with
/// esp-box-emu); this is the Tab5's side of them: the games, the images,
/// the settings and the USB drive.
class Gui {
public:
  struct Config {
    std::string metadata_filename = "library/games.json";
    /// Where the carts keep their save states (the list shows each game's
    /// count)
    std::string save_dir{tab5::content::resolve("states")};
    espp::Logger::Verbosity log_level{espp::Logger::Verbosity::WARN};
  };

  static emu_ui::Metrics &shared_metrics();
  explicit Gui(const Config &config);
  ~Gui();

  void ready_to_play(bool new_state) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (new_state) launch_error_.clear();
    ready_to_play_ = new_state;
  }
  bool ready_to_play() const { return ready_to_play_; }
  void report_launch_error(std::string message) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    launch_error_ = std::move(message);
  }
  std::string launch_error() const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    return launch_error_;
  }
  void record_content_hash(const std::string &path, const std::string &hash) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (!content_hashes_.contains(path) && content_hashes_.size() >= 128)
      content_hashes_.erase(content_hashes_.begin());
    content_hashes_[path] = hash;
  }

  std::optional<RomInfo> get_selected_rom() const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (focused_rom_ < 0 || focused_rom_ >= (int)rom_infos_.size()) {
      return std::nullopt;
    }
    return rom_infos_[focused_rom_];
  }

  /// Stop driving LVGL (while a game is running)
  void pause();
  /// Reload the rom list if the metadata changed, and start driving LVGL again
  void resume();

  void update_rom_list();
  /// Give the launcher rom_infos_ (with their save counts), keeping
  /// `selected_path` selected
  void show_games(const std::string &selected_path);
  /// Re-read metadata.csv even if its modification time did not change (FAT
  /// times are coarse, and without a set clock they may not move at all).
  void reload_rom_list();
  bool set_usb_drive_from_main(bool on);
  void process_requests();
  /// The games in list order (grouped by console).
  std::vector<RomInfo> rom_infos() const {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    return rom_infos_;
  }

  /// Select the first game whose name or rom path contains `text` (a game
  /// the console filter hides lifts the filter)
  bool select_rom_matching(std::string_view text);
  tab5::library::Page query_catalog(const tab5::library::Query &query);
  bool select_catalog_rom(const RomInfo &rom);

  /// The consoles that have games, in list order.
  std::vector<Console> consoles() const;
  /// The console whose games the list shows, if it is filtered.
  std::optional<Console> console_filter() const;
  /// Show only `console`'s games (nullopt: every console's); false if it has
  /// none.
  bool set_console_filter(std::optional<Console> console);

  /// The settings the launcher shows, for the USB link: {settings: {name:
  /// value}, limits: {name: [min, max]}} (volume, muted, brightness, video,
  /// resume).
  std::string settings_json() const;
  /// Change one of them as its control in the launcher does (shown, applied,
  /// saved once the changes stop); false for an unknown name or a value out
  /// of range.
  bool set_setting(const std::string &name, int value);

  /// The launcher screen (RGB565, little endian, rows packed), for the USB
  /// link's screenshot when no game runs; empty if it cannot be drawn.
  struct Snapshot {
    size_t width{0}, height{0};
    std::vector<uint8_t> pixels;
  };
  Snapshot snapshot();

protected:
  struct CatalogJob {
    tab5::library::Query query;
    tab5::library::Page result;
    std::atomic<bool> cancelled{false};
    bool done{false};
  };
  std::shared_ptr<CatalogJob> catalog_job_;
  std::atomic<bool> library_loading_{false};
  bool indexed_library_{false}, library_job_{false}, library_ready_{false};
  tab5::library::Query library_query_;
  tab5::library::Page library_result_;
  std::atomic<uint64_t> library_generation_{0};
  uint64_t library_result_generation_{0};
  std::atomic<bool> library_cancel_{false};
  lv_obj_t *library_pages_{nullptr}, *library_page_label_{nullptr},
      *library_system_{nullptr};
  size_t library_total_{0}, library_page_{0};
  std::string library_error_;
  std::string launch_error_;
  void build_library_pages();
  void request_library_page(size_t page);
  void accept_library_page();
  void refresh_library();
  static constexpr int kWorkerCoverBox = 360;
  struct CoverEntry {
    std::string path;
    lv_draw_buf_t *image;
    uint64_t used;
  };
  std::map<std::string, CoverEntry> cover_cache_;
  std::map<std::string, std::string> content_hashes_;
  std::deque<std::string> cover_pending_;
  std::set<std::string> cover_requested_;
  std::vector<CoverEntry> cover_ready_;
  std::string active_cover_path_, active_icon_path_, hash_pending_,
      hash_system_, ready_hash_path_, ready_hash_;
  int ready_count_{0};
  uint64_t cover_tick_{0};
  std::thread cover_thread_;
  mutable std::mutex cover_mutex_;
  std::condition_variable cover_cv_;
  bool cover_stop_{false}, cover_paused_{true}, cover_busy_{false};
  void start_cover_worker();
  void stop_cover_worker();
  void pause_cover_worker(bool pause);
  const void *request_cover(const std::string &path, bool priority);
  void request_save_count(size_t index);
  void accept_cover_results();
  emu_ui::Metrics load_fonts();
  void build_library_tools();
  void refresh_library_tools();
  void open_search();
  void open_controls();
  std::unique_ptr<ControlsEditor> controls_editor_;
  void close_search(bool apply);
  void read_search_keys();
  void set_library_view(int view);
  void toggle_favorite();
  void apply_theme();
  void apply_color_filter(lv_obj_t *object);
  std::vector<emu_ui::Row> settings_rows();
  void set_usb_drive(bool on);
  void update();
  void on_volume(const std::vector<uint8_t> &data);
  void on_battery(const std::vector<uint8_t> &data);

  /// Decode a JPEG on the SD card into a new RGB565 draw buffer (nullptr if
  /// it cannot), scaled to fit a box x box square (box > 0)

  lv_draw_buf_t *decode_jpeg_hw(const std::string &path, int box);
  static lv_draw_buf_t *fit_image(const uint8_t *src, int w, int h, int stride,
                                  int box);
  const void *cover(size_t index);
  const void *console_art(const std::string &key);

  emu_ui::Metrics metrics_{};
  std::array<lv_font_t *, 4> external_fonts_{};
  lv_color_filter_dsc_t light_filter_{};
  std::unique_ptr<lv_theme_t> external_theme_;
  lv_opa_t theme_opacity_{0};
  std::string current_theme_;
  std::string theme_error_;
  unsigned fonts_loaded_{0};
  std::vector<size_t> visible_indices_;
  lv_obj_t *library_tools_{nullptr}, *favorite_label_{nullptr},
      *search_dialog_{nullptr}, *search_text_{nullptr};
  std::array<uint8_t, 32> search_previous_keys_{};
  uint8_t shortcut_previous_{0};
  bool search_old_test_mode_{false};
  std::vector<RomInfo> rom_infos_;
  std::atomic<int> usb_request_{-1};
  std::atomic<bool> reload_requested_{false};
  std::atomic<int> focused_rom_{-1};
  std::unique_ptr<emu_ui::Launcher> launcher_;
  std::atomic<bool> settings_dirty_{false}, battery_dirty_{true};
  lv_obj_t *platform_status_{nullptr};

  std::string usb_note_;
  bool usb_drive_shown_{false};

  std::string metadata_filename_;
  std::string save_dir_;
  std::unordered_map<std::string, int>
      save_counts_; ///< per rom stem (emu_ui::count_saves)
  std::filesystem::file_time_type metadata_last_modified_{};

  std::atomic<bool> paused_{false};
  std::atomic<bool> ready_to_play_{false};
  espp::HighResolutionTimer task_{{
      .name = "Gui Task",
      .callback = std::bind(&Gui::update, this),
  }};
  espp::Logger logger_;
  std::recursive_mutex &mutex_{
      Tab5Emu::lvgl_mutex()}; // shared: LVGL is not thread-safe
};
