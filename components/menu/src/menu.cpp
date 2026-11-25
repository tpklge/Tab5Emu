#include "menu.hpp"
#include "battery_level.hpp"
#include "gui.hpp"
#include "paludo_platform.hpp"
#include "lvgl_private.h"

#include <cmath>

#include <esp_heap_caps.h>

// The screenshot buffers (1.2 MB) are allocated when the menu opens and freed
// when it closes, not held while the game runs: Jedi Knight's level load
// needs that room. heap_caps_malloc does not throw: if a game leaves no
// 600 KB block, the menu shows no screenshot.
void Menu::ImageBuffer::allocate() {
  if (!data) data = static_cast<uint8_t *>(heap_caps_malloc(SCREENSHOT_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void Menu::ImageBuffer::release() {
  heap_caps_free(data);
  data = nullptr;
}

Menu::Menu(const Config &config)
    : paused_image_path_(config.paused_image_path)
    , console_key_(config.console_key),rom_sha256_(config.rom_sha256)
    , action_callback_(config.action_callback)
    , slot_image_callback_(config.slot_image_callback)
    , logger_({.tag = "Menu", .level = config.log_level}) {
  using K = emu_ui::Row::Kind;
  auto &emu = Tab5Emu::get();
  std::vector<emu_ui::Row> settings;
  settings.push_back({.label="Controles",.kind=K::ACTION,.set=[this](int){controls_editor_=std::make_unique<ControlsEditor>(console_key_,rom_sha256_,true);},.symbol=LV_SYMBOL_KEYBOARD});
  settings.push_back({.label = "Volume", .kind = K::SLIDER, .step = 5,
                      .get = [&emu] { return (int)std::lround(emu.volume()); },
                      .set = [&emu](int v) { emu.volume(v); },
                      .symbol = LV_SYMBOL_VOLUME_MAX});
  settings.push_back({.label = "Mute", .kind = K::TOGGLE, .get = [&emu] { return emu.is_muted() ? 1 : 0; },
                      .set = [&emu](int v) { emu.mute(v != 0); },
                      .symbol = LV_SYMBOL_MUTE});
  settings.push_back({.label = "Brightness", .kind = K::SLIDER, .min = 5, .step = 5,
                      .get = [&emu] { return (int)std::lround(emu.brightness()); },
                      .set = [&emu](int v) { emu.brightness(v); },
                      .symbol = LV_SYMBOL_EYE_OPEN});
  settings.push_back({.label = "Video scaling", .kind = K::CHOICE, .choices = {"Original", "Fit", "Fill"},
                      .get = [&emu] { return (int)emu.video_setting(); },
                      .set = [&emu](int v) { emu.video_setting(static_cast<VideoSetting>(v)); },
                      .symbol = LV_SYMBOL_IMAGE});
  settings.push_back({.label="Voltar ao Launcher do Paludo",.kind=K::ACTION,
    .set=[this](int){if(paludo_launcher_available())action_callback_(Action::LAUNCHER);},
    .note=[] {return std::string("Salva o jogo e as preferências antes de sair");},
    .symbol=LV_SYMBOL_HOME});
  {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    view_ = std::make_unique<emu_ui::PauseMenu>(emu_ui::PauseMenu::Config{
        .metrics = Gui::shared_metrics(),
        .keypad = emu.keypad_indev(),
        .slots = static_cast<int>(MAX_SLOT) + 1,
        .on_action = [this](emu_ui::PauseMenu::Action a) { on_action(a); },
        .on_slot =
            [this](int slot) {
              selected_slot_ = slot;
              update_slot_display();
            },
        .settings = std::move(settings),
    });
    view_->set_game(config.game_title, config.console_key);
    status_label_ = lv_label_create(view_->screen());
    lv_obj_set_pos(status_label_, 210, 580);
    lv_obj_set_size(status_label_, 370, 76);
    lv_obj_set_style_text_font(status_label_, Gui::shared_metrics().font_small, 0);
    lv_obj_set_style_text_color(status_label_, lv_color_white(), 0);
    lv_obj_set_style_pad_all(status_label_, 6, 0);
    lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
  }
  using namespace std::placeholders;
  espp::EventManager::get().add_subscriber(volume_changed_topic, "menu", std::bind(&Menu::on_volume, this, _1), 4 * 1024);
  logger_.info("Menu created");
}

Menu::~Menu() {
  espp::EventManager::get().remove_subscriber(volume_changed_topic, "menu");
  task_.stop();
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  view_.reset();
  pause_image_data_.release();
  slot_image_data_.release();
}

void Menu::select_slot(int slot) {
  if (slot < 0) slot = MAX_SLOT;
  if (slot > (int)MAX_SLOT) slot = 0;
  selected_slot_ = slot;
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  view_->set_slot(slot); // (calls on_slot when it changes)
  update_slot_display();
}

void Menu::pause() {
  resume_presented_frame_rate();
  paused_ = true;
  task_.stop();
  // wait out a timer callback already in lv_timer_handler() (stop() does not)
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  controls_editor_.reset();
  // the game gets the screenshots' room back
  view_->set_pause_image(nullptr);
  view_->set_slot_image(nullptr);
  pause_image_data_.release();
  slot_image_data_.release();
}

void Menu::resume() {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  pause_image_data_.allocate();
  slot_image_data_.allocate();
  if (!pause_image_data_.data || !slot_image_data_.data) {
    logger_.warn("No room for the menu's screenshots (PSRAM largest block {} KB)",
                 heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) / 1024);
  }
  update_pause_image();
  update_slot_display();
  view_->set_fps(get_presented_fps());
  const auto battery = Tab5Emu::get().battery_status();
  view_->set_battery(tab5::power::estimated_percent(battery.is_present,battery.voltage_v), battery.is_charging);
  view_->show();
  lv_obj_invalidate(view_->screen());
  task_.periodic(16 * 1000);
  paused_ = false;
}

void Menu::click(Action action) {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  if(action==Action::LAUNCHER){action_callback_(action);return;}
  view_->click(static_cast<emu_ui::PauseMenu::Action>(action));
}

void Menu::set_status(std::string text, bool error) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  status_text_ = std::move(text);
  status_error_ = error && !status_text_.empty();
  lv_label_set_text(status_label_, status_text_.c_str());
  lv_obj_set_style_bg_color(status_label_, lv_color_hex(error ? 0x702020 : 0x163d32), 0);
  lv_obj_set_style_bg_opa(status_label_, LV_OPA_COVER, 0);
  if (status_text_.empty()) lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
}
std::string Menu::operation_error() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return status_error_ ? status_text_ : std::string{};
}
tab5::content::FileBytes Menu::snapshot() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (paused_) return {};
  auto *screen = controls_editor_ && !controls_editor_->closed()
                     ? controls_editor_->screen() : view_->screen();
  lv_obj_update_layout(screen);
  tab5::content::FileBytes pixels;
  pixels.length = 1280 * 720 * 2;
  pixels.bytes.reset(static_cast<unsigned char *>(std::malloc(pixels.size())));
  if (!pixels.data()) return {};
  lv_draw_buf_t buffer;
  if (lv_draw_buf_init(&buffer, 1280, 720, LV_COLOR_FORMAT_RGB565, 2560,
                      pixels.data(), pixels.size()) != LV_RESULT_OK ||
      lv_snapshot_take_to_draw_buf(screen, LV_COLOR_FORMAT_RGB565, &buffer) != LV_RESULT_OK)
    return {};
  return pixels;
}

void Menu::on_action(emu_ui::PauseMenu::Action a) {
  // (the same order: RESUME, RESET, SAVE, LOAD, QUIT)
  const auto action = static_cast<Action>(a);
  action_callback_(action);
  if (action == Action::SAVE && operation_error().empty()) {
    update_slot_display(); // the new screenshot
  } else if (action == Action::LOAD && operation_error().empty()) {
    update_pause_image(); // the loaded state
  }
}

void Menu::update() {
  if (!paused_) {
    std::lock_guard<std::recursive_mutex> lk(mutex_);
    if (controls_editor_) controls_editor_->update();
    lv_timer_handler();
  }
}

void Menu::on_volume(const std::vector<uint8_t> &) {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  view_->refresh_settings();
}

void Menu::update_pause_image() {
  const bool ok = emu_ui::load_screenshot(paused_image_path_.c_str(), pause_image_data_.span(), pause_image_desc_);
  view_->set_pause_image(ok ? &pause_image_desc_ : nullptr);
}

void Menu::update_slot_display() {
  std::lock_guard<std::recursive_mutex> lk(mutex_);
  const auto path = slot_image_callback_ ? slot_image_callback_() : std::string{};
  const bool ok = !path.empty() && emu_ui::load_screenshot(path.c_str(), slot_image_data_.span(), slot_image_desc_);
  view_->set_slot_image(ok ? &slot_image_desc_ : nullptr);
}
