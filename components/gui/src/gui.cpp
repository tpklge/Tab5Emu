#include "gui.hpp"
#include "battery_level.hpp"
#include "usb_msc_gate.hpp"
#include "lvgl_private.h"

#include "content_storage.hpp"
#include "emu-ui/saves.hpp"
#include "esp_chip_info.h"
#include "esp_private/esp_clk.h"
#include "font_resources.hpp"
#include "frame_metrics.hpp"
#include "paludo_platform.hpp"
#include "wall_clock.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <esp_app_desc.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include "driver/jpeg_decode.h"

#include "lvgl_private.h" // (lv_image_decoder_dsc_t)

using namespace std::chrono_literals;

namespace {

// the Console of a console key (rom_info's ConsoleInfo::key)
std::optional<Console> console_of_key(const std::string &key) {
  for (int c = 0; c < static_cast<int>(Console::UNKNOWN); c++) {
    if (key == console_info(static_cast<Console>(c)).key)
      return static_cast<Console>(c);
  }
  return std::nullopt;
}

std::string version_text() {
  const auto *app = esp_app_get_description();
  const auto *part = esp_ota_get_running_partition();
#ifdef TAB5_BUILD_GBA_RV32
  return fmt::format("{}  {} | RV32 experimental, sem SNES",
                     app ? app->version : "", part ? part->label : "");
#endif
  return fmt::format("{}  {}", app ? app->version : "",
                     part ? part->label : "");
}

struct SettingLimit {
  const char *name;
  int min, max;
};
// (the same ranges as the launcher's own controls)
constexpr SettingLimit kSettingLimits[] = {
    {"volume", 0, 100},        {"muted", 0, 1},
    {"brightness", 5, 100},    {"video", 0, 2},
    {"resume", 0, 1},          {"idle_seconds", 0, 3600},
    {"idle_in_game", 0, 1},    {"input_test", 0, 1},
#ifdef TAB5_BUILD_GBA_RV32
    {"gba_cpu", 0, 1},
#else
    {"gba_cpu", 0, 0},
#endif
    {"md_six_buttons", 0, 1},
    {"library_page", 0, 1562}, {"clock_epoch", 1577836800, 2147483647}};

} // namespace

Gui::Gui(const Config &config)
    : metadata_filename_(config.metadata_filename), save_dir_(config.save_dir),
      logger_({.tag = "Gui", .level = config.log_level}) {
  {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    metrics_ = load_fonts();
    launcher_ = std::make_unique<emu_ui::Launcher>(emu_ui::Launcher::Config{
        .metrics = metrics_,
#if defined(TAB5_SM64_NATIVE)
        .brand = "SM64 nativo",
#else
        .brand = "tab5-emu",
#endif
        .keypad = Tab5Emu::get().keypad_indev(),
        .cover = [this](size_t i) { return cover(i); },
        .console_art =
            [this](const std::string &key) { return console_art(key); },
        .on_play =
            [this](size_t i) {
              if (i < visible_indices_.size()) {
                focused_rom_ = visible_indices_[i];
                ready_to_play(true);
              }
            },
        .on_select =
            [this](size_t i) {
              if (i < visible_indices_.size()) {
                focused_rom_ = visible_indices_[i];
                request_save_count(focused_rom_);
                refresh_library_tools();
              }
            },
        .settings = settings_rows(),
        .version = version_text(),
    });
    update_rom_list();
    launcher_->show();
    build_library_tools();
    build_library_pages();
    platform_status_ = lv_label_create(launcher_->screen());
    lv_obj_set_pos(platform_status_, 510, 88);
    lv_obj_set_size(platform_status_, 740, 24);
    lv_label_set_long_mode(platform_status_, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(platform_status_, &lv_font_montserrat_16, 0);
  }
  start_cover_worker();
  task_.periodic(16 * 1000);
  using namespace std::placeholders;
  espp::EventManager::get().add_subscriber(volume_changed_topic, "gui",
                                           std::bind(&Gui::on_volume, this, _1),
                                           4 * 1024);
  espp::EventManager::get().add_subscriber(
      battery_topic, "gui", std::bind(&Gui::on_battery, this, _1), 5 * 1024);
}

Gui::~Gui() {
  espp::EventManager::get().remove_subscriber(volume_changed_topic, "gui");
  espp::EventManager::get().remove_subscriber(battery_topic, "gui");
  task_.stop();
  stop_cover_worker();
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  launcher_.reset();
  if (external_theme_)
    lv_display_set_theme(lv_display_get_default(), external_theme_->parent);
  for (auto *font : external_fonts_)
    if (font)
      lv_binfont_destroy(font);
  for (auto &[path, entry] : cover_cache_)
    if (entry.image) {
      lv_image_cache_drop(entry.image);
      lv_draw_buf_destroy(entry.image);
    }
  for (auto &entry : cover_ready_)
    if (entry.image) {
      lv_image_cache_drop(entry.image);
      lv_draw_buf_destroy(entry.image);
    }
}

void Gui::pause() {
  paused_ = true;
  task_.stop();
  // wait out a timer callback already in lv_timer_handler() (stop() does not)
  {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (search_dialog_)
      close_search(false);
    controls_editor_.reset();
  }
  pause_cover_worker(true);
}

void Gui::resume() {
  pause_cover_worker(false);
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  // (a game started from the PC, emulink launch, may have left the picker open)
  launcher_->close_picker();
  save_counts_.clear();
  update_rom_list();
  if(indexed_library_ && !library_loading_)request_library_page(library_page_);
  launcher_->refresh_settings();
  launcher_->show();
  lv_obj_invalidate(launcher_->screen());
  task_.periodic(16 * 1000);
  paused_ = false;
}

void Gui::update() {
  if (paused_)
    return;
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  // the USB link can start or end the drive mode too (emulink drive on /
  // off; an eject alone does not, the hand-over is never automatic): follow
  // it, and pick up what the PC copied over
  const bool drive_on = Tab5Emu::get().usb_msc_host_has_card();
  if (indexed_library_ && !library_loading_) {
    const auto local = launcher_->filter();
    if (local) {
      {
        std::lock_guard<std::mutex> work(cover_mutex_);
        library_query_.system = *local;
      }
      launcher_->set_filter(std::nullopt);
      request_library_page(0);
    }
  }
  if (!drive_on) {
    accept_library_page();
    accept_cover_results();
  }
  const auto &board = Tab5Emu::get();
  const auto battery = board.battery_status();
  const auto battery_percent=tab5::power::estimated_percent(battery.is_present,battery.voltage_v);
  const auto status = fmt::format(
      "SD: {} | A164 I2C: {} | bateria: {}",
      board.sdcard() ? (drive_on ? "USB" : "OK") : "ausente",
      board.i2c_keyboard_present() ? "OK" : "ausente",
      battery_percent>=0 ? fmt::format("{:.2f} V (~{}% estimado){}", battery.voltage_v,battery_percent,
                                       battery.is_charging ? " carregando" : "")
                         : "sem leitura");
  lv_label_set_text(platform_status_,
                    (launch_error_.empty() ? status + " | F2 Fav F3 Busca F4 Lista" +
                     (!library_error_.empty() ? " | " + library_error_ :
                      !theme_error_.empty() ? " | " + theme_error_ :
                      fonts_loaded_ < 4 ? " | Fontes SD incompletas; fonte interna" :
                      rom_infos_.empty() && !library_loading_ ? " | Biblioteca vazia ou filtro sem resultados" : "")
                     : launch_error_)
                        .c_str());
  if (battery_dirty_.exchange(false))
    launcher_->set_battery(battery_percent, battery.is_charging);
  static unsigned refresh_tick = 0;
  if (settings_dirty_.exchange(false) ||
      (++refresh_tick % 30 == 0 && launcher_->settings_open()))
    launcher_->refresh_settings();
  if (drive_on != usb_drive_shown_) {
    usb_drive_shown_ = drive_on;
    usb_note_ =
        drive_on
            ? "The PC has the SD card. Eject it there, then switch this off."
            : "";
    launcher_->set_play_enabled(!drive_on);
    launcher_->refresh_settings();
    if (!drive_on)
      reload_requested_ = true;
  }
  read_search_keys();
  if (controls_editor_) controls_editor_->update();
  apply_theme();
  lv_timer_handler();
}

std::vector<emu_ui::Row> Gui::settings_rows() {
  using K = emu_ui::Row::Kind;
  auto &emu = Tab5Emu::get();
  // (saved to the SD card by the main loop once the changes stop)
  std::vector<emu_ui::Row> rows;
  rows.push_back({.label = "Controles",
                  .kind = K::ACTION,
                  .set = [this](int) { open_controls(); },
                  .symbol = LV_SYMBOL_KEYBOARD});
  rows.push_back({.label = "Volume",
                  .kind = K::SLIDER,
                  .step = 5,
                  .get = [&emu] { return (int)std::lround(emu.volume()); },
                  .set =
                      [&emu](int v) {
                        emu.volume(v);
                        AppSettings::get().modify(
                            [v](Settings &s) { s.volume = v; });
                      },
                  .symbol = LV_SYMBOL_VOLUME_MAX});
  rows.push_back({.label = "Mute",
                  .kind = K::TOGGLE,
                  .get = [&emu] { return emu.is_muted() ? 1 : 0; },
                  .set =
                      [&emu](int v) {
                        emu.mute(v != 0);
                        AppSettings::get().modify(
                            [v](Settings &s) { s.muted = v != 0; });
                      },
                  .symbol = LV_SYMBOL_MUTE});
  rows.push_back({.label = "Brightness",
                  .kind = K::SLIDER,
                  .min = 5,
                  .step = 5,
                  .get = [&emu] { return (int)std::lround(emu.brightness()); },
                  .set =
                      [&emu](int v) {
                        emu.brightness(v);
                        AppSettings::get().modify(
                            [v](Settings &s) { s.brightness = v; });
                      },
                  .symbol = LV_SYMBOL_EYE_OPEN});
  rows.push_back({.label = "Video scaling",
                  .kind = K::CHOICE,
                  .choices = {"Original", "Fit", "Fill"},
                  .get = [&emu] { return (int)emu.video_setting(); },
                  .set =
                      [&emu](int v) {
                        emu.video_setting(static_cast<VideoSetting>(v));
                        AppSettings::get().modify(
                            [v](Settings &s) { s.video_setting = v; });
                      },
                  .symbol = LV_SYMBOL_IMAGE});
  rows.push_back(
      {.label = "Resume last save",
       .kind = K::TOGGLE,
       .get =
           [] {
             return AppSettings::get().get_settings().resume_last_save ? 1 : 0;
           },
       .set =
           [](int v) {
             AppSettings::get().modify(
                 [v](Settings &s) { s.resume_last_save = v != 0; });
           },
       .symbol = LV_SYMBOL_REFRESH});
  rows.push_back(
      {.label = "USB drive",
       .kind = K::TOGGLE,
       .get = [&emu] { return emu.usb_msc_host_has_card() ? 1 : 0; },
       .set = [this](int v) { set_usb_drive(v != 0); },
       .note =
           [this] {
             return usb_note_.empty()
                        ? std::string(
                              "The SD card as a USB drive on the USB-C port")
                        : usb_note_;
           },
       .symbol = LV_SYMBOL_USB});
  rows.push_back({.label = "Teste de teclas (HID)",
                  .kind = K::TOGGLE,
                  .get = [&emu] { return emu.keyboard_test_mode() ? 1 : 0; },
                  .set = [&emu](int on) { emu.keyboard_test_mode(on != 0); },
                  .note =
                      [] {
                        const auto keys = Tab5Emu::get().keyboard_state();
                        std::string held = "Pressionadas:";
                        for (unsigned i = 0; i < 256; ++i)
                          if ((keys.keys[i >> 3] >> (i & 7)) & 1) {
                            constexpr char hex[] = "0123456789ABCDEF";
                            held.push_back(' ');
                            held.push_back(hex[i >> 4]);
                            held.push_back(hex[i & 15]);
                          }
                        return held + " | " +
                               Tab5Emu::get().keyboard_profile_status() +
                               (Tab5Emu::get().keyboard_test_mode()
                                    ? " | teste: toque para sair"
                                    : "");
                      },
                  .symbol = LV_SYMBOL_KEYBOARD});
#ifdef TAB5_BUILD_GBA_RV32
  rows.push_back(
      {.label = "CPU GBA (proximo boot do jogo)",
       .kind = K::CHOICE,
       .choices = {"Interpretador", "RV32 experimental"},
       .get = [] { return AppSettings::get().ui_settings().gba_cpu; },
       .set =
           [](int v) {
             AppSettings::get().modify_ui(
                 [v](UiSettings &u) { u.gba_cpu = v; });
           },
       .symbol = LV_SYMBOL_SETTINGS});
#endif
  rows.push_back({.label = "Controle Mega Drive",
                  .kind = K::CHOICE,
                  .choices = {"3 botoes", "6 botoes"},
                  .get =
                      [] {
                        return AppSettings::get().ui_settings().md_six_buttons
                                   ? 1
                                   : 0;
                      },
                  .set =
                      [](int v) {
                        AppSettings::get().modify_ui(
                            [v](UiSettings &u) { u.md_six_buttons = v != 0; });
                      },
                  .symbol = LV_SYMBOL_KEYBOARD});
  rows.push_back(
      {.label = "Tema",
       .kind = K::CHOICE,
       .choices = {"Consoles", "Escuro", "Claro"},
       .get =
           [] {
             const auto t = AppSettings::get().ui_settings().theme;
             return t == "light" ? 2 : t == "dark" ? 1 : 0;
           },
       .set =
           [](int v) {
             AppSettings::get().modify_ui([v](UiSettings &u) {
               u.theme = v == 2 ? "light" : v == 1 ? "dark" : "system";
             });
           },
       .symbol = LV_SYMBOL_IMAGE});
  rows.push_back(
      {.label = "Apagar tela por inatividade",
       .kind = K::CHOICE,
       .choices = {"Desativado", "1 minuto", "2 minutos", "5 minutos"},
       .get =
           [] {
             const int n = AppSettings::get().ui_settings().idle_seconds;
             return n == 300 ? 3 : n == 120 ? 2 : n ? 1 : 0;
           },
       .set =
           [](int v) {
             AppSettings::get().modify_ui([v](UiSettings &u) {
               u.idle_seconds = v == 3 ? 300 : v == 2 ? 120 : v ? 60 : 0;
             });
           },
       .symbol = LV_SYMBOL_EYE_CLOSE});
  rows.push_back(
      {.label = "Inatividade também durante jogo",
       .kind = K::TOGGLE,
       .get =
           [] { return AppSettings::get().ui_settings().idle_in_game ? 1 : 0; },
       .set =
           [](int value) {
             AppSettings::get().modify_ui(
                 [&](UiSettings &u) { u.idle_in_game = value != 0; });
           },
       .note =
           [] {
             return std::string("Desativado: cutscenes mantêm a tela acesa");
           },
       .symbol = LV_SYMBOL_VIDEO});
  rows.push_back(
      {.label = "Voltar ao Launcher do Paludo",
       .kind = K::ACTION,
       .set =
           [](int) {
             if (paludo_launcher_available())
               paludo_request_return();
           },
       .note =
           [] {
             return paludo_launcher_available()
                        ? std::string("Salva preferencias e reinicia pelo boot "
                                      "de uso unico")
                        : std::string("Contrato de retorno nao detectado");
           },
       .symbol = LV_SYMBOL_HOME});
  return rows;
}

// USB drive mode: hand the SD card to a PC. Nothing may touch the card
// meanwhile, so Play is off; when it ends the list is reloaded so anything
// copied over shows up.
void Gui::set_usb_drive(bool on) {
  if (on) {
    ready_to_play_ = false;
    launcher_->set_play_enabled(false);
  }
  usb_request_ = on ? 1 : 0;
}
bool Gui::set_usb_drive_from_main(bool on) {
  if (on)
    pause_cover_worker(true);
  bool done = false;
  {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    auto &emu = Tab5Emu::get();
    if (on)
      done = emu.initialize_usb_msc();
    else {
      emu.deinitialize_usb_msc();
      done = !emu.usb_msc_host_has_card();
    }
    usb_drive_shown_ = emu.usb_msc_host_has_card();
    launcher_->set_play_enabled(!usb_drive_shown_);
    usb_note_ = usb_drive_shown_
                    ? "O computador controla o SD; ejete antes de desativar"
                    : "";
  }
  if (!on && done)
    reload_rom_list();
  if (!Tab5Emu::get().usb_msc_host_has_card())
    pause_cover_worker(false);
  return done;
}
void Gui::process_requests() {
  const int request = usb_request_.exchange(-1);
  if (request >= 0)
    set_usb_drive_from_main(request != 0);
  if (reload_requested_.exchange(false) &&
      !Tab5Emu::get().usb_msc_host_has_card())
    reload_rom_list();
}
void Gui::reload_rom_list() {
  if (Tab5Emu::get().usb_msc_host_has_card()) {
    reload_requested_ = true;
    return;
  }
  pause_cover_worker(true);
  {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    launcher_->set_games({}, -1); // release every LVGL image reference first
    for (auto &[path, item] : cover_cache_)
      if (item.image) {
        lv_image_cache_drop(item.image);
        lv_draw_buf_destroy(item.image);
      }
    cover_cache_.clear();
    save_counts_.clear();
    content_hashes_.clear();
    {
      std::lock_guard<std::mutex> work(cover_mutex_);
      for (auto &item : cover_ready_)
        if (item.image) {
          lv_image_cache_drop(item.image);
          lv_draw_buf_destroy(item.image);
        }
      cover_ready_.clear();
      cover_pending_.clear();
      cover_requested_.clear();
      hash_pending_.clear();
      ready_hash_path_.clear();
    }
    metadata_last_modified_ = std::filesystem::file_time_type::min();
    current_theme_.clear();
    update_rom_list();
  }
  pause_cover_worker(false);
}

void Gui::on_volume(const std::vector<uint8_t> &) {
  // EventManager's subscriber has a 4 KiB stack. LVGL callbacks + fmt do
  // not fit there (confirmed by core dump); refresh on the GUI timer only.
  settings_dirty_ = true;
}

void Gui::on_battery(const std::vector<uint8_t> &) {
  // BSP 1.4.6 uses an uncalibrated linear voltage placeholder for percent.
  // Display measured volts above until a calibrated estimate is available.
  battery_dirty_ = true;
}

void Gui::update_rom_list() {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  std::error_code ec;
  auto path = tab5::content::resolve(metadata_filename_);
  auto mtime = std::filesystem::last_write_time(path, ec);
  // Namespace is system / real ROM SHA / core-v1, as used by Cart.
  // Catalog reload is bounded; each per-game directory contains at most slots.
  if (!ec && mtime == metadata_last_modified_ && !rom_infos_.empty()) {
    show_games(get_selected_rom() ? get_selected_rom()->rom_path
                                  : std::string());
    return;
  }
  metadata_last_modified_ = mtime;
  // keep the selected game selected across a reload (its index may change);
  // at boot that is the last game played
  std::string selected_path;
  if (focused_rom_ >= 0 && focused_rom_ < (int)rom_infos_.size()) {
    selected_path = rom_infos_[focused_rom_].rom_path;
  } else {
    selected_path = AppSettings::get().get_settings().last_rom;
  }
  indexed_library_ = std::filesystem::is_regular_file(
      tab5::content::resolve("cache/library-v1.t5i"), ec);
  if (indexed_library_) {
    rom_infos_.clear();
    show_games({});
    request_library_page(0);
    return;
  }
  rom_infos_ = load_library();
#if !defined(ENABLE_N64)
  std::erase_if(rom_infos_,[](const RomInfo&r){return r.platform==Emulator::N64;});
#endif
#if !defined(ENABLE_GBA)
  std::erase_if(rom_infos_,[](const RomInfo&r){return r.platform==Emulator::GBA;});
#endif
#if !defined(ENABLE_GENESIS)
  std::erase_if(rom_infos_,[](const RomInfo&r){return r.platform==Emulator::SEGA_GENESIS || r.platform==Emulator::SEGA_MEGA_DRIVE;});
#endif
#if !defined(ENABLE_GBC)
  // Diagnostic profiles can trade this core for the R4300 probe. Preserve
  // the SD catalog while keeping unavailable systems out of the chooser.
  std::erase_if(rom_infos_, [](const RomInfo &rom) {
    return rom.platform == Emulator::GAMEBOY ||
           rom.platform == Emulator::GAMEBOY_COLOR;
  });
#endif
#if !defined(ENABLE_SNES)
  std::erase_if(rom_infos_, [](const RomInfo &rom) {
    return rom.platform == Emulator::SNES;
  });
#endif
  // grouped by console, in Console order; metadata.csv order within a console
  std::stable_sort(rom_infos_.begin(), rom_infos_.end(),
                   [](const RomInfo &a, const RomInfo &b) {
                     return console_of(a.platform) < console_of(b.platform);
                   });
  save_counts_.clear();
  show_games(selected_path);
  if (library_pages_)
    lv_obj_add_flag(library_pages_, LV_OBJ_FLAG_HIDDEN);
}

void Gui::show_games(const std::string &selected_path) {
  int selected = -1;
  std::vector<emu_ui::Game> games;
  visible_indices_.clear();
  const auto ui = AppSettings::get().ui_settings();
  auto fold = [](std::string text) {
    for (size_t i = 0; i < text.size(); ++i) {
      auto c = static_cast<unsigned char>(text[i]);
      if (c == 0xc3 && i + 1 < text.size()) {
        const auto d = static_cast<unsigned char>(text[i + 1]);
        char replacement = 0;
        if ((d >= 0x80 && d <= 0x85) || (d >= 0xa0 && d <= 0xa5))
          replacement = 'a';
        else if (d == 0x87 || d == 0xa7)
          replacement = 'c';
        else if ((d >= 0x88 && d <= 0x8b) || (d >= 0xa8 && d <= 0xab))
          replacement = 'e';
        else if ((d >= 0x8c && d <= 0x8f) || (d >= 0xac && d <= 0xaf))
          replacement = 'i';
        else if ((d >= 0x92 && d <= 0x96) || (d >= 0xb2 && d <= 0xb6))
          replacement = 'o';
        else if ((d >= 0x99 && d <= 0x9c) || (d >= 0xb9 && d <= 0xbc))
          replacement = 'u';
        if (replacement) {
          text.replace(i, 2, 1, replacement);
          continue;
        }
      }
      if (c >= 'A' && c <= 'Z')
        text[i] = c - 'A' + 'a';
    }
    return text;
  };
  const auto query = fold(ui.search);
  std::vector<size_t> order;
  if (ui.library_view == 2) {
    for (const auto &path : ui.recent)
      for (size_t i = 0; i < rom_infos_.size(); ++i)
        if (rom_infos_[i].rom_path == path)
          order.push_back(i);
  } else {
    for (size_t i = 0; i < rom_infos_.size(); ++i)
      order.push_back(i);
  }
  for (size_t i : order) {
    const auto &rom = rom_infos_[i];
    const auto &info = console_info(console_of(rom.platform));
    if (ui.library_view == 1 &&
        std::find(ui.favorites.begin(), ui.favorites.end(), rom.rom_path) ==
            ui.favorites.end())
      continue;
    if (!query.empty() &&
        fold(rom.name + " " + rom.description + " " + rom.rom_path)
                .find(query) == std::string::npos)
      continue;
    const auto it = save_counts_.find(rom.rom_path);
    if (rom.rom_path == selected_path)
      selected = games.size();
    visible_indices_.push_back(i);
    games.push_back({.title = rom.name,
                     .subtitle = rom.description,
                     .console_key = info.key,
                     .console_name = info.name,
                     .console_short = info.short_name,
                     .saves = it == save_counts_.end() ? 0 : it->second});
  }
  focused_rom_ = -1;
  launcher_->set_games(std::move(games), selected);
  const int at = launcher_->selected();
  focused_rom_ = at >= 0 && size_t(at) < visible_indices_.size()
                     ? int(visible_indices_[at])
                     : -1;
  refresh_library_tools();
  if (focused_rom_ >= 0)
    request_save_count(focused_rom_);
}

std::vector<Console> Gui::consoles() const {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  std::vector<Console> out;
  for (const auto &key : launcher_->consoles()) {
    if (auto c = console_of_key(key))
      out.push_back(*c);
  }
  return out;
}

std::optional<Console> Gui::console_filter() const {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  if (indexed_library_) {
    std::lock_guard<std::mutex> job(cover_mutex_);
    return console_of_key(library_query_.system);
  }
  const auto f = launcher_->filter();
  return f ? console_of_key(*f) : std::nullopt;
}

bool Gui::set_console_filter(std::optional<Console> console) {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  if (indexed_library_) {
    const auto key = console ? console_info(*console).key : "";
    {
      std::lock_guard<std::mutex> job(cover_mutex_);
      library_query_.system = key;
    }
    request_library_page(0);
    return true;
  }
  return launcher_->set_filter(
      console ? std::optional<std::string>(console_info(*console).key)
              : std::nullopt);
}

bool Gui::select_rom_matching(std::string_view text) {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  // Like LIST_ROMS, the legacy USB selector operates on the loaded page.
  // Never scan a large index synchronously inside a main-queue launch job:
  // its timeout cannot undo a launch after the callback has begun running.
  // an exact path or name first (one ROM's path can contain another's), then
  // the first game containing the text
  for (const bool exact : {true, false}) {
    for (size_t i = 0; i < rom_infos_.size(); i++) {
      const auto &rom = rom_infos_[i];
      if (exact ? (rom.rom_path == text || rom.name == text)
                : (rom.name.find(text) != std::string::npos ||
                   rom.rom_path.find(text) != std::string::npos)) {
        // (a game started from the link must not leave the picker over the
        // list)
        if (indexed_library_) {
          std::lock_guard<std::mutex> work(cover_mutex_);
          ++library_generation_;
          library_job_ = library_ready_ = false;
          library_loading_ = false;
        }
        launcher_->close_picker();
        AppSettings::get().modify_ui([](UiSettings &u) {
          u.search.clear();
          u.library_view = 0;
        });
        show_games(rom.rom_path);
        const auto visible =
            std::find(visible_indices_.begin(), visible_indices_.end(), i);
        launcher_->select(visible - visible_indices_.begin());
        focused_rom_ = i;
        return true;
      }
    }
  }
  return false;
}

std::string Gui::settings_json() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  auto &emu = Tab5Emu::get();
  const auto s = AppSettings::get().get_settings();
  std::string limits;
  for (const auto &l : kSettingLimits) {
    limits += fmt::format("{}\"{}\":[{},{}]", limits.empty() ? "" : ",", l.name,
                          l.min, l.max);
  }
  auto result = fmt::format(
      "{{\"settings\":{{\"volume\":{},\"muted\":{},\"brightness\":{},\"video\":"
      "{},\"resume\":{},\"input_test\":{}}},"
      "\"limits\":{{{}}}}}",
      (int)std::lround(emu.volume()), emu.is_muted() ? 1 : 0,
      (int)std::lround(emu.brightness()), (int)emu.video_setting(),
      s.resume_last_save ? 1 : 0, emu.keyboard_test_mode() ? 1 : 0, limits);
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  const auto battery = emu.battery_status();
  const auto kb = emu.keyboard_state();
  std::string keys;
  for (unsigned i = 0; i < 256; ++i)
    if ((kb.keys[i >> 3] >> (i & 7)) & 1) {
      if (!keys.empty())
        keys.push_back(',');
      keys += std::to_string(i);
    }
  result.pop_back();
  result += fmt::format(
      ",\"platform\":{{\"chip_revision\":{},\"cpu_hz\":{},\"sd_present\":{},"
      "\"a164_present\":{},\"keys_hid\":[{}],\"buttons\":{},\"keyboard_"
      "buttons\":{},\"battery_present\":{},"
      "\"battery_volts\":{:.3f},\"battery_ma\":{:.3f},\"charging\":{},\"paludo_"
      "return\":{}}},\"gba_metrics\":{}}}",
      chip.revision, esp_clk_cpu_freq(), emu.sdcard() != nullptr,
      emu.i2c_keyboard_present(), keys, emu.gamepad_state().buttons,
      emu.keyboard_gamepad_state().buttons, battery.is_present,
      battery.voltage_v, battery.current_ma, battery.is_charging,
      paludo_launcher_available(), gba_metrics_json());
  result.pop_back();
  result += fmt::format(
      ",\"resources\":{{\"fonts_loaded\":{},\"font_fallback\":{},\"theme_error\":\"{}\"}},\"library\":{{\"indexed\":{},\"page\":{},\"total\":{},\"error\":\"{}\"}"
      "},\"gba_cpu_requested\":{},\"md_six_buttons\":{}}}",
      fonts_loaded_, fonts_loaded_ < 4, theme_error_, indexed_library_, library_page_, library_total_, library_error_,
      AppSettings::get().ui_settings().gba_cpu,
      AppSettings::get().ui_settings().md_six_buttons);
  result.pop_back();
  result +=
      fmt::format(",\"clock_epoch\":{}}}", tab5::content::wall_clock_seconds());
  result.pop_back();
  result += fmt::format(",\"library_loading\":{}}}", library_loading_.load());
  const auto audio = emu.audio_stats();
  const auto msc=tab5::usb::msc_io_snapshot();
  result.pop_back();
  result+=fmt::format(",\"msc_io\":{{\"host_allowed\":{},\"blocked_reads\":{},\"blocked_writes\":{},\"blocked_ready\":{}}}}}",msc.allowed,msc.blocked_reads,msc.blocked_writes,msc.blocked_ready);
  result.pop_back();
  result += fmt::format(
      ",\"audio\":{{\"source_rate\":{},\"hardware_rate\":{},\"source_frames\":{"
      "},\"output_frames\":{},\"dropped_bytes\":{},\"max_submit_gap_us\":{},"
      "\"clock_ticks\":{},\"clock_hz\":{}}}}}",
      audio.source_rate, audio.hardware_rate, audio.source_frames,
      audio.output_frames, audio.dropped_bytes, audio.max_gap_us,
      audio.clock_ticks, audio.clock_hz);
  result.pop_back();
  result += fmt::format(",\"screen_sleeping\":{},\"idle_seconds\":{}}}",
                        emu.screen_sleeping(),
                        AppSettings::get().ui_settings().idle_seconds);
  const auto sink = tab5::audio_probe::snapshot();
  result.pop_back();
  result += fmt::format(
      ",\"audio_sink\":{{\"blocks\":{},\"partial_blocks\":{},\"empty_blocks\":{"
      "},\"missing_frames\":{},\"max_write_gap_us\":{},\"write_errors\":{},\"late_blocks\":{},\"late_frames\":{},\"startup_blocks\":{},\"startup_partial_blocks\":{},\"startup_empty_blocks\":{},\"startup_missing_frames\":{},\"startup_write_errors\":{}}}}}",
      sink.blocks, sink.partial_blocks, sink.empty_blocks, sink.missing_frames,
      sink.max_write_gap_us, sink.write_errors,sink.late_blocks,sink.late_frames,sink.startup_blocks,sink.startup_partial_blocks,sink.startup_empty_blocks,sink.startup_missing_frames,sink.startup_write_errors);
  return result;
}

bool Gui::set_setting(const std::string &name, int value) {
  const auto *limit =
      std::find_if(std::begin(kSettingLimits), std::end(kSettingLimits),
                   [&](const SettingLimit &l) { return name == l.name; });
  if (limit == std::end(kSettingLimits) || value < limit->min ||
      value > limit->max || (name == "idle_seconds" && value && value < 15)) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  if (name == "gba_cpu" || name == "md_six_buttons") {
    if (value < 0 || value > 1)
      return false;
    AppSettings::get().modify_ui([&](UiSettings &u) {
      if (name == "gba_cpu")
        u.gba_cpu = value;
      else
        u.md_six_buttons = value != 0;
    });
    return true;
  }
  if (name == "library_page") {
    if (!indexed_library_ || value < 0 || value > 1562)
      return false;
    request_library_page(value);
    return true;
  }
  if (name == "clock_epoch")
    return tab5::content::set_wall_clock_seconds(value);
  auto &emu = Tab5Emu::get();
  // (saved to the SD card by the main loop once the changes stop, as from the
  // launcher)
  AppSettings::get().modify([&](Settings &s) {
    if (name == "volume")
      s.volume = value;
    else if (name == "muted")
      s.muted = value != 0;
    else if (name == "brightness")
      s.brightness = value;
    else if (name == "video")
      s.video_setting = value;
    else if (name == "resume")
      s.resume_last_save = value != 0;
  });
  if (name == "idle_in_game") {
    AppSettings::get().modify_ui(
        [&](UiSettings &u) { u.idle_in_game = value != 0; });
    emu.inactivity_in_game(value != 0);
  }
  if (name == "idle_seconds") {
    AppSettings::get().modify_ui(
        [&](UiSettings &u) { u.idle_seconds = value; });
    emu.inactivity_seconds(value);
  }
  if (name == "volume")
    emu.volume(value);
  else if (name == "muted")
    emu.mute(value != 0);
  else if (name == "brightness")
    emu.brightness(value);
  else if (name == "video")
    emu.video_setting(static_cast<VideoSetting>(value));
  else if (name == "input_test")
    emu.keyboard_test_mode(value != 0);
  launcher_->refresh_settings(); // the launcher shows it
  return true;
}

Gui::Snapshot Gui::snapshot() {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  Snapshot shot;
  auto *screen = controls_editor_ && !controls_editor_->closed()
                     ? controls_editor_->screen() : lv_screen_active();
  if (!screen) {
    return shot;
  }
  lv_obj_update_layout(screen);
  const int32_t w = lv_obj_get_width(screen), h = lv_obj_get_height(screen);
  const uint32_t stride =
      lv_draw_buf_width_to_stride(w, LV_COLOR_FORMAT_RGB565);
  shot.pixels.resize(stride * h);
  lv_draw_buf_t buf;
  if (lv_draw_buf_init(&buf, w, h, LV_COLOR_FORMAT_RGB565, stride,
                       shot.pixels.data(),
                       shot.pixels.size()) != LV_RESULT_OK ||
      lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, &buf) !=
          LV_RESULT_OK) {
    shot.pixels.clear();
    return shot;
  }
  // (rows packed: the reader expects w * 2 bytes per row)
  if (stride != static_cast<uint32_t>(w) * 2) {
    for (int32_t y = 1; y < h; y++) {
      memmove(shot.pixels.data() + y * w * 2, shot.pixels.data() + y * stride,
              w * 2);
    }
    shot.pixels.resize(w * h * 2);
  }
  shot.width = w;
  shot.height = h;
  return shot;
}

// emu-ui's launcher shows the cover in a 360 px square
// (LV_IMAGE_ALIGN_CONTAIN). Scaled to fit it once, here, LVGL draws the cover
// as it is rather than scaling it in software on every redraw.
constexpr int kCoverBox = 360;

// The selected game's boxart, decoded once per selection (the launcher has
// already let go of the previous one).
const void *Gui::cover(size_t index) {
  if (index >= visible_indices_.size() ||
      visible_indices_[index] >= rom_infos_.size())
    return nullptr;
  active_cover_path_ = rom_infos_[visible_indices_[index]].boxart_path;
  return request_cover(active_cover_path_, true);
}
const void *Gui::console_art(const std::string &key) {
  active_icon_path_ = tab5::content::resolve("assets/icons/" + key + ".jpg");
  return request_cover(active_icon_path_, false);
}

namespace {

// The P4's JPEG decoder (made on first use; nullptr if it cannot be)
jpeg_decoder_handle_t jpeg_engine() {
  static jpeg_decoder_handle_t engine = nullptr;
  static bool tried = false;
  if (!tried) {
    tried = true;
    jpeg_decode_engine_cfg_t cfg{};
    cfg.timeout_ms = 500;
    if (jpeg_new_decoder_engine(&cfg, &engine) != ESP_OK)
      engine = nullptr;
  }
  return engine;
}

// The frame header of a baseline JPEG in memory: the offset of its height
// (then width) field and its MCU size; -1 if there is none (progressive...)
int find_frame_header(const uint8_t *d, size_t n, int &mcu_w, int &mcu_h) {
  size_t i = 2; // (after SOI)
  while (i + 4 <= n) {
    if (d[i] != 0xFF)
      return -1;
    const uint8_t m = d[i + 1];
    if (m == 0xFF) { // fill byte
      i++;
      continue;
    }
    if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) {
      i += 2;
      continue;
    }
    const size_t len = (d[i + 2] << 8) | d[i + 3];
    if (m == 0xC0 || m == 0xC1) {
      // P Y(2) X(2) Nf, then per component: id, Hi:Vi, Tq; the decoder's MCU
      // is the first component's sampling factors x 8
      if (len < 11 || i + 2 + len > n)
        return -1;
      const int h = d[i + 11] >> 4, v = d[i + 11] & 0x0f;
      if (!h || !v)
        return -1;
      mcu_w = 8 * h;
      mcu_h = 8 * v;
      return static_cast<int>(i + 5);
    }
    if (m == 0xDA || m == 0xD9 ||
        (m >= 0xC2 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC)) {
      return -1; // a scan first, or not baseline
    }
    i += 2 + len;
  }
  return -1;
}

struct FreeDeleter {
  void operator()(void *p) const { free(p); }
};

} // namespace

// A new RGB565 draw buffer of `src` (rows `stride` bytes apart), scaled
// (bilinear) to fit a box x box square with its longer side box long, or as
// it is (box 0)
lv_draw_buf_t *Gui::fit_image(const uint8_t *src, int w, int h, int stride,
                              int box) {
  int dw = w, dh = h;
  if (box > 0) {
    if (w >= h) {
      dw = box;
      dh = std::max(1, (h * box + w / 2) / w);
    } else {
      dh = box;
      dw = std::max(1, (w * box + h / 2) / h);
    }
  }
  lv_draw_buf_t *out = nullptr;
  {
    std::lock_guard<std::recursive_mutex> lk(Tab5Emu::lvgl_mutex());
    out = lv_draw_buf_create(dw, dh, LV_COLOR_FORMAT_RGB565, LV_STRIDE_AUTO);
  }
  if (!out)
    return nullptr;
  if (dw == w && dh == h) {
    for (int y = 0; y < h; y++)
      memcpy(out->data + y * out->header.stride, src + y * stride, w * 2);
    return out;
  }
  // Separable bilinear: each source row is interpolated across once (into a
  // two-row cache in internal RAM), then output rows blend two of those. The
  // pixels spread as 0x07E0F81F (green, red, blue, with room above each) so
  // one multiply weighs all three, with 5 bit weights.
  const int32_t step_x = (w << 16) / dw, step_y = (h << 16) / dh;
  auto src_pos = [](int i, int32_t step, int n) {
    return std::clamp<int32_t>(i * step + step / 2 - 0x8000, 0, (n - 1) << 16);
  };
  constexpr uint32_t kMask = 0x07E0F81Fu;
  auto spread = [](uint32_t c) { return (c | (c << 16)) & kMask; };
  // per output column: the two source columns and the weight of the second
  const size_t bytes = static_cast<size_t>(dw) *
                       (2 * sizeof(uint32_t) + 2 * sizeof(uint16_t) + 1);
  std::unique_ptr<uint8_t, FreeDeleter> scratch(static_cast<uint8_t *>(
      heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  if (!scratch)
    scratch.reset(static_cast<uint8_t *>(malloc(bytes)));
  if (!scratch) {
    lv_draw_buf_destroy(out);
    return nullptr;
  }
  auto *row_a = reinterpret_cast<uint32_t *>(scratch.get());
  auto *row_b = row_a + dw;
  auto *xa = reinterpret_cast<uint16_t *>(row_b + dw);
  auto *xb = xa + dw;
  auto *wx = reinterpret_cast<uint8_t *>(xb + dw);
  for (int x = 0; x < dw; x++) {
    const int32_t sx = src_pos(x, step_x, w);
    xa[x] = sx >> 16;
    xb[x] = std::min(xa[x] + 1, w - 1);
    wx[x] = (sx >> 11) & 31;
  }
  auto across = [&](int sy, uint32_t *dst) {
    const auto *r = reinterpret_cast<const uint16_t *>(src + sy * stride);
    for (int x = 0; x < dw; x++) {
      const uint32_t f = wx[x];
      dst[x] =
          ((spread(r[xa[x]]) * (32 - f) + spread(r[xb[x]]) * f) >> 5) & kMask;
    }
  };
  int have_a = -1, have_b = -1; // the source rows in row_a / row_b
  for (int y = 0; y < dh; y++) {
    const int32_t sy = src_pos(y, step_y, h);
    const int y0 = sy >> 16, y1 = std::min(y0 + 1, h - 1);
    if (have_a != y0) {
      if (have_b == y0) {
        std::swap(row_a, row_b);
        std::swap(have_a, have_b);
      } else {
        across(y0, row_a);
        have_a = y0;
      }
    }
    if (have_b != y1) {
      across(y1, row_b);
      have_b = y1;
    }
    const uint32_t f = (sy >> 11) & 31;
    auto *row =
        reinterpret_cast<uint16_t *>(out->data + y * out->header.stride);
    for (int x = 0; x < dw; x++) {
      const uint32_t c = ((row_a[x] * (32 - f) + row_b[x] * f) >> 5) & kMask;
      row[x] = static_cast<uint16_t>(c | (c >> 16));
    }
  }
  return out;
}

// A baseline JPEG through the P4's JPEG decoder, straight to RGB565: a cover
// decoded and scaled takes ~30 ms, where LVGL's software decoder took ~75 ms
// for the decode alone. nullptr if it cannot (decode_image() then falls back
// to LVGL's).
lv_draw_buf_t *Gui::decode_jpeg_hw(const std::string &path, int box) {
  auto *engine = jpeg_engine();
  if (!engine)
    return nullptr;
  FILE *f = fopen(path.c_str(), "rb");
  if (!f)
    return nullptr;
  fseek(f, 0, SEEK_END);
  const long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size < 4 || size > 256 * 1024) {
    fclose(f);
    return nullptr;
  }
  jpeg_decode_memory_alloc_cfg_t in_cfg{.buffer_direction =
                                            JPEG_DEC_ALLOC_INPUT_BUFFER};
  size_t in_capacity = 0;
  std::unique_ptr<uint8_t, FreeDeleter> in(static_cast<uint8_t *>(
      jpeg_alloc_decoder_mem(size, &in_cfg, &in_capacity)));
  const bool read =
      in && fread(in.get(), 1, size, f) == static_cast<size_t>(size);
  fclose(f);
  if (!read)
    return nullptr;
  // The decoder refuses a picture whose width x height is not a multiple of
  // 8 (two in five of the covers here), though it decodes whole MCUs anyway:
  // give the frame header the MCU-rounded size (the same MCUs, so the same
  // scan) and crop the result back.
  int mcu_w = 8, mcu_h = 8;
  const int sof = find_frame_header(in.get(), size, mcu_w, mcu_h);
  if (sof < 0)
    return nullptr;
  uint8_t *dims = in.get() + sof;
  const int h = (dims[0] << 8) | dims[1], w = (dims[2] << 8) | dims[3];
  if (!w || !h || w > 360 || h > 360)
    return nullptr;
  const int pw = (w + mcu_w - 1) / mcu_w * mcu_w,
            ph = (h + mcu_h - 1) / mcu_h * mcu_h;
  dims[0] = ph >> 8;
  dims[1] = ph & 0xff;
  dims[2] = pw >> 8;
  dims[3] = pw & 0xff;
  jpeg_decode_memory_alloc_cfg_t out_cfg{.buffer_direction =
                                             JPEG_DEC_ALLOC_OUTPUT_BUFFER};
  size_t out_capacity = 0;
  std::unique_ptr<uint8_t, FreeDeleter> out(
      static_cast<uint8_t *>(jpeg_alloc_decoder_mem(
          static_cast<size_t>(pw) * ph * 2, &out_cfg, &out_capacity)));
  if (!out)
    return nullptr;
  jpeg_decode_cfg_t cfg{};
  cfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
  cfg.rgb_order =
      JPEG_DEC_RGB_ELEMENT_ORDER_BGR; // (little-endian RGB565, as LVGL's)
  cfg.conv_std = JPEG_YUV_RGB_CONV_STD_BT601;
  uint32_t out_size = 0;
  if (jpeg_decoder_process(engine, &cfg, in.get(), size, out.get(),
                           out_capacity, &out_size) != ESP_OK ||
      out_size != static_cast<uint32_t>(pw) * ph * 2) {
    return nullptr;
  }
  return fit_image(out.get(), w, h, pw * 2, box);
}
