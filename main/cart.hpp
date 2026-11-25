#pragma once
#include "paludo_platform.hpp"
#include "pause_shortcut.hpp"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include "file_read.hpp"
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>
#include <esp_timer.h>

#include "sdkconfig.h"

#include "logger.hpp"

#include "menu.hpp"
#include "rom_info.hpp"
#include "settings.hpp"
#include "content_paths.hpp"
#include "content_storage.hpp"
#include "tab5-emu.hpp"

/// This class is the base class for all carts (games / emulator cores).
/// It provides the following functionality:
/// - in-game pause menu (resume / reset / save / load / quit)
/// - screenshot capture for the save slots
/// - video setting (original / fit / fill)
class Cart {
public:
  using Pixel = Tab5Emu::Pixel;

  /// Configuration for the Cart class
  struct Config {
    RomInfo info;             ///< rom info
    bool copy_romdata = true; ///< copy the romdata into the shared rom buffer
    espp::Logger::Verbosity verbosity = espp::Logger::Verbosity::WARN; ///< verbosity for the logger
  };

  explicit Cart(const Config &config)
      : info_(config.info)
      , savedir_(FS_PREFIX + "/" + SAVE_DIR)
      , logger_({.tag = "Cart", .level = config.verbosity}) {
    logger_.info("ctor");
    // The main loop computes the actual content hash before creating a cart.
    savedir_ += "/" + std::string(console_info(console_of(info_.platform)).key) + "/" + info_.content_hash + "/core-v1";
    auto &emu = Tab5Emu::get();
    reset_presented_frame_rate();
    emu.clear_audio();
    emu.clear_screen();
    // drop a menu request left over from the GUI (e.g. a tap in the hot
    // corner while choosing the game) so the cart doesn't open the pause menu
    // before the game has started
    (void)emu.menu_requested();

    if (config.copy_romdata) {
      logger_.info("Copying romdata...");
      rom_size_bytes_ = emu.copy_file_to_romdata(get_rom_filename());
      romdata_ = emu.romdata();
    }

    menu_ = std::make_unique<Menu>(Menu::Config{
        .paused_image_path = get_paused_image_path(),
        .action_callback = std::bind(&Cart::on_menu_action, this, std::placeholders::_1),
        .slot_image_callback = [this]() -> std::string { return get_screenshot_path(false); },
        .game_title = info_.name,
        .rom_sha256=info_.content_hash,
        .console_key = console_info(console_of(info_.platform)).key,
        .log_level = espp::Logger::Verbosity::WARN});

    std::error_code ec;
    if (!std::filesystem::exists(savedir_, ec)) {
      std::filesystem::create_directories(savedir_, ec);
    }
  }

  virtual ~Cart() {
    logger_.info("Base dtor");
    Tab5Emu::get().release_video_buffers();
    Tab5Emu::get().free_romdata();
  }

  /// Where the save states are (the launcher counts them)
  static std::string save_dir() { return FS_PREFIX + "/" + SAVE_DIR; }

  std::string get_rom_filename() const { return info_.rom_path; }

  virtual void reset() { logger_.info("Base reset"); }

  virtual void load() {
    logger_.info("Base loading...");
    state_done_ = true; // (an override that fails does not get here)
    copy_file(get_screenshot_path(true), get_paused_image_path());
  }

  virtual void save() {
    logger_.info("Base saving...");
    state_done_ = true;
    copy_file(get_paused_image_path(), get_screenshot_path(true));
  }

  /// Save the current (native resolution) frame to a file as
  /// [w:u16be][h:u16be][RGB565 pixels]
  virtual bool screenshot(std::string_view filename) {
    logger_.info("Base screenshot: {}", filename);
    auto [width, height] = get_video_size();
    std::span<uint8_t> frame = get_video_buffer();
    if (frame.empty()) {
      return false;
    }
    uint8_t header[4] = {(uint8_t)(width >> 8), (uint8_t)(width & 0xFF), (uint8_t)(height >> 8),
                         (uint8_t)(height & 0xFF)};
    return tab5::content::atomic_file_write(std::string(filename), sizeof(header) + frame.size(),
      [&](FILE *file) {
        return fwrite(header, 1, sizeof(header), file) == sizeof(header) &&
               fwrite(frame.data(), 1, frame.size(), file) == frame.size();
      });
  }

  virtual bool is_running() const { return running_; }
  virtual bool loaded() const { return true; }
  virtual std::string core_metrics_json() const{return "{}";}
  virtual std::string load_error() const { return "Falha ao abrir o jogo. Confira a ROM e os saves."; }

  /// Start from the save state in `slot` (the one this game was last saved
  /// to / loaded from), if the cart supports that and the slot holds a
  /// state. The load is done the way the pause menu does it, on the first
  /// frame for which resume_after_frames() has passed.
  void resume_from_slot(int slot) {
    if (!supports_resume()) {
      return;
    }
    menu_->select_slot(slot);
    if (get_save_path().empty()) {
      logger_.info("resume: no state in slot {}", slot);
      return;
    }
    resume_pending_ = true;
  }

  /// Quit the way the pause menu's Quit does (the game is paused first, then
  /// the cart stops; the caller stops calling run()); for the automated
  /// launch tests.
  void quit_as_from_menu() {
    pre_menu();
    running_ = !persist_before_exit();
    if (running_) menu_->set_status("Falha ao gravar save. Saida cancelada; confira cartao e espaco livre.", true);
  }

  /// Save the state to `slot` (with its screenshot) the way the pause menu's
  /// Save does, without showing the menu; for the USB link's test control.
  /// Call from the task that runs the cart (between run() calls).
  bool save_from_host(int slot) {
    menu_->select_slot(slot);
    pre_menu();
    screenshot(get_paused_image_path());
    state_done_ = false;
    save();
    report_state_result(true);
    if (state_done_)
      remember_slot();
    if (running_)
      post_menu();
    return state_done_;
  }

  /// Load the state in `slot` the way the pause menu's Load does; false if
  /// the slot is empty. Same rules as save_from_host().
  bool load_from_host(int slot) {
    menu_->select_slot(slot);
    if (get_save_path().empty()) {
      menu_->set_status("Slot vazio ou arquivo indisponivel. Escolha outro estado.", true);
      return false;
    }
    pre_menu();
    state_done_ = false;
    load();
    report_state_result(false);
    if (state_done_)
      remember_slot();
    if (running_)
      post_menu();
    return state_done_;
  }

  const RomInfo &rom_info() const { return info_; }
  /// Frames the game has drawn so far (for the USB link's status: frame-rate
  /// tests); 0 where the cart does not count them.
  virtual uint32_t game_frames() const { return 0; }
  bool pause_menu_open() const { return menu_ && !menu_->is_paused(); }
  void set_pause_poll(std::function<void()> poll) { pause_poll_ = std::move(poll); }
  std::string operation_error() const { return menu_ ? menu_->operation_error() : std::string{}; }
  tab5::content::FileBytes pause_snapshot() { return menu_ ? menu_->snapshot() : tab5::content::FileBytes{}; }

  /// The frame a screenshot captures: its size and its RGB565 pixels (for
  /// the USB link's screenshot, which copies it without going through a file)
  std::pair<size_t, size_t> frame_size() const { return get_video_size(); }
  std::span<uint8_t> frame_pixels() const { return get_video_buffer(); }
  /// Hold the game still (true) while the USB link reads a screenshot
  /// straight from its frame buffer, and let it go on (false). The main loop
  /// is held meanwhile, which is enough for a core that runs in run(); a core
  /// with its own tasks pauses them, as for the pause menu.
  virtual void capture_hold(bool hold) {}

  /// The game reads other files from its ROM's folder while it runs (a PC
  /// port's data files): the USB link then leaves that folder alone
  virtual bool uses_rom_folder() const { return false; }

  /// Open the pause menu over the game, click the button of `action` there
  /// and resume, as a player would; for the automated launch tests.
  void menu_action_as_from_menu(Menu::Action action) {
    using namespace std::chrono_literals;
    auto &emu = Tab5Emu::get();
    pre_menu();
    screenshot(get_paused_image_path());
    menu_->resume();
    std::this_thread::sleep_for(1s);
    menu_->click(action);
    std::this_thread::sleep_for(1s);
    if (!menu_->is_paused()) {
      menu_->click(Menu::Action::RESUME);
    }
    (void)emu.menu_requested();
    emu.clear_screen();
    if (running_)
      post_menu();
  }

  /// Run one iteration of the game loop; returns false once the user quits.
  /// Quit the game at the next run() call, as the pause menu's Quit does
  /// (for the USB link's test control; any task).
  void request_quit() { quit_requested_ = true; }

  virtual bool run() {
    if (quit_requested_) {
      pre_menu();
      if (persist_before_exit()) { running_ = false; return false; }
      quit_requested_ = false;
      logger_.error("Save failed; keeping game loaded");
      menu_->set_status("Falha ao gravar save. Saida cancelada; confira cartao e espaco livre.", true);
      post_menu();
    }
    running_ = true;
    auto &emu = Tab5Emu::get();
    if (resume_pending_ && ++frames_run_ > resume_after_frames()) {
      resume_pending_ = false;
      logger_.info("resume: loading slot {}", get_selected_save_slot());
      pre_menu();
      state_done_ = false;
      load();
      report_state_result(false);
      post_menu();
    }
    // the menu is requested by the boot button, by the touch "menu" hot corner
    // or by start+select on a gamepad
    auto state = emu.gamepad_state();
    const bool chord = pause_shortcut_.update(state.start, state.select, esp_timer_get_time());
    bool show_menu = emu.menu_requested() || (chord && emu.touch_profile()!=tab5::input::Profile::MegaDrive6 && emu.touch_profile()!=tab5::input::Profile::N64);
    if (show_menu) {
      logger_.info("Menu pressed!");
      pre_menu();
      screenshot(get_paused_image_path());
      menu_->resume();
      while (!menu_->is_paused()) {
        using namespace std::chrono_literals;
        if (pause_poll_) pause_poll_();
        std::this_thread::sleep_for(16ms);
      }
      // taps on the menu also reach the in-game touch handler (and its
      // "menu" hot corner); drop any request they generated
      (void)emu.menu_requested();
      emu.clear_screen();
      if (running_)
        post_menu();
    }
    return running_;
  }

protected:
  std::function<void()> pause_poll_;
  tab5::input::PauseShortcut pause_shortcut_;
  static constexpr size_t SCREEN_WIDTH = Tab5Emu::lcd_width();
  static constexpr size_t SCREEN_HEIGHT = Tab5Emu::lcd_height();
  static inline const std::string FS_PREFIX = tab5::content::root;
#if CONFIG_TAB5_EMU_AUTOQUIT_STATES
  // the automated save / load test writes its own saves, not the player's
  static inline const std::string SAVE_DIR = "saves-soak";
#else
  static inline const std::string SAVE_DIR = "states";
#endif

  static void copy_file(const std::string &from, const std::string &to) {
    tab5::content::FileBytes bytes;
    if (tab5::content::read_file(from, 640 * 480 * 2 + 4, bytes) == tab5::content::ReadStatus::Ok)
      (void)tab5::content::atomic_write(to, bytes.data(), bytes.size());
  }

  void report_state_result(bool saving) {
    menu_->set_status(state_done_ ? (saving ? "Estado salvo." : "Estado carregado.")
                                 : (saving ? "Falha ao salvar estado. Confira cartao e espaco livre."
                                           : "Falha ao carregar estado. Arquivo invalido ou erro de leitura."),
                      !state_done_);
  }

  virtual void on_menu_action(Menu::Action action) {
    switch (action) {
    case Menu::Action::RESUME:
      menu_->set_status({});
      menu_->pause();
      break;
    case Menu::Action::RESET:
      reset();
      menu_->pause();
      break;
    case Menu::Action::QUIT:
      if (persist_before_exit()) { running_ = false; menu_->pause(); }
      else menu_->set_status("Falha ao gravar save. Saida cancelada; confira cartao e espaco livre.", true);
      break;
    case Menu::Action::LAUNCHER:
      if (persist_before_exit()) {
        paludo_request_return();
        running_=false;
        menu_->pause();
      } else menu_->set_status("Falha ao gravar save. Retorno ao Launcher cancelado.", true);
      break;
    case Menu::Action::SAVE:
    case Menu::Action::LOAD:
      // (the slot becomes the resume slot only if the core took it)
      state_done_ = false;
      action == Menu::Action::SAVE ? save() : load();
      report_state_result(action == Menu::Action::SAVE);
      if (state_done_)
        remember_slot();
      break;
    default:
      break;
    }
  }

  /// Whether resume_from_slot() may load a state right after the game
  /// started. The emulator cores say yes; the native ports keep their own
  /// saves and are started fresh.
  virtual bool supports_resume() const { return false; }
  /// Frames to run before the resume load (a core that needs its game to
  /// get going first, e.g. a PICO-8 cart's _init, says how long).
  virtual int resume_after_frames() const { return 0; }

  /// The slot just saved to / loaded from becomes the one resume uses.
  void remember_slot() const {
    auto &settings = AppSettings::get();
    settings.set_last_slot(get_rom_filename(), get_selected_save_slot());
    settings.save();
  }

  virtual std::string get_save_extension() const { return ".sav"; }
  std::string get_screenshot_extension() const { return ".bin"; }

  virtual bool persist_before_exit() { return true; }
  virtual void pre_menu() { Tab5Emu::get().audio_playback_expected(false); Tab5Emu::get().game_active(false); Tab5Emu::get().discard_audio(); Tab5Emu::get().touch_controls(false); Tab5Emu::get().wait_menu_input_release(); Tab5Emu::get().drain_video(); }
  virtual void post_menu() { Tab5Emu::get().game_active(true); Tab5Emu::get().touch_controls(true); handle_video_setting(); }

  virtual std::pair<size_t, size_t> get_video_size() const { return {SCREEN_WIDTH, SCREEN_HEIGHT}; }
  virtual std::span<uint8_t> get_video_buffer() const { return {}; }

  virtual void set_original_video_setting() = 0;
  virtual void set_fit_video_setting() = 0;
  virtual void set_fill_video_setting() = 0;

  /// Helpers for the video settings of a fixed native size (emulator cores):
  /// original = largest integer scale that fits, fit = largest scale that
  /// keeps the aspect ratio (in the PPA's 1/16 steps), fill = whole screen.
  void display_original(size_t w, size_t h) {
    const size_t s = std::max<size_t>(1, std::min(Tab5Emu::get().game_view_width() / w, SCREEN_HEIGHT / h));
    Tab5Emu::get().display_size(w * s, h * s);
  }
  void display_fit(size_t w, size_t h) {
    float s = std::min(static_cast<float>(Tab5Emu::get().game_view_width()) / w, static_cast<float>(SCREEN_HEIGHT) / h);
    s = std::floor(s * 16.0f) / 16.0f;
    Tab5Emu::get().display_size(static_cast<size_t>(w * s), static_cast<size_t>(h * s));
  }
  void display_fill() { Tab5Emu::get().display_size(SCREEN_WIDTH, SCREEN_HEIGHT); }

  virtual void handle_video_setting() {
    switch (Tab5Emu::get().video_setting()) {
    case VideoSetting::ORIGINAL:
      set_original_video_setting();
      break;
    case VideoSetting::FIT:
      set_fit_video_setting();
      break;
    case VideoSetting::FILL:
      set_fill_video_setting();
      break;
    default:
      break;
    }
  }

  int get_selected_save_slot() const { return menu_->get_selected_slot(); }

  std::string get_save_path(bool bypass_exist_check = false) const {
    namespace fs = std::filesystem;
    auto save_path = savedir_ + "/" + fs::path(get_rom_filename()).stem().string() +
                     fmt::format("_{}", get_selected_save_slot()) + get_save_extension();
    if (bypass_exist_check) {
      return save_path;
    }
    return tab5::content::readable_generation(save_path);
  }

  /// The image file of a PlayStation game's memory card (the raw 128 KB
  /// format PC emulators read), named after the game's data directory
  /// (saves/tpm_card1.mcd): the game's own saves, which unlike save states
  /// do not depend on the firmware build.
  std::string get_memory_card_path(int card = 1) const {
    namespace fs = std::filesystem;
    const auto game = fs::path(get_rom_filename()).parent_path().filename().string();
    return savedir_ + "/" + game + fmt::format("_card{}.mcd", card);
  }

  std::string get_paused_image_path() const { return savedir_ + "/paused" + get_screenshot_extension(); }

  std::string get_screenshot_path(bool bypass_exist_check = false) const {
    auto save_path = get_save_path(bypass_exist_check);
    if (!save_path.empty()) {
      return save_path + get_screenshot_extension();
    }
    return "";
  }

  // set by the base save() / load(), which a failing override does not call
  bool state_done_{false};
  std::atomic<bool> running_{false};
  bool resume_pending_{false};
  std::atomic<bool> quit_requested_{false};
  int frames_run_{0};
  size_t rom_size_bytes_{0};
  uint8_t *romdata_{nullptr};
  RomInfo info_;
  std::string savedir_;
  std::unique_ptr<Menu> menu_;
  espp::Logger logger_;
};
