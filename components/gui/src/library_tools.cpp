#include "cJSON.h"
#include "content_storage.hpp"
#include "font_resources.hpp"
#include "gui.hpp"
#include "lvgl_private.h"
#include <algorithm>
#include <cstring>
#include "file_read.hpp"
#if !LV_USE_COLOR_FILTER
#error "SD themes require LVGL color filters"
#endif

emu_ui::Metrics &Gui::shared_metrics() {
  static auto metrics = emu_ui::Metrics::large_1280x720();
  return metrics;
}

emu_ui::Metrics Gui::load_fonts() {
  auto metrics = emu_ui::Metrics::large_1280x720();
  const lv_font_t **slots[] = {&metrics.font_small, &metrics.font_body,
                               &metrics.font_title, &metrics.font_large};
  for (size_t i = 0; i < external_fonts_.size(); ++i) {
    const auto &asset = tab5::fonts::resources[i];
    tab5::content::FileBytes bytes;
    if (!tab5::content::read_exact_file(tab5::content::resolve(asset.path), asset.bytes, bytes))
      continue;
    std::array<uint8_t, 32> digest{};
    if (!tab5::content::digest_bytes(bytes.data(), bytes.size(), digest))
      continue;
    constexpr char hex[] = "0123456789abcdef";
    std::string sha;
    for (uint8_t v : digest) {
      sha += hex[v >> 4];
      sha += hex[v & 15];
    }
    if (sha != asset.sha256)
      continue; // Pass exactly the bytes hashed: no second SD read.
    auto *font = lv_binfont_create_from_buffer(bytes.data(), bytes.size());
    if (font) {
      font->fallback = *slots[i];
      external_fonts_[i] = font;
      *slots[i] = font;
      ++fonts_loaded_;
    }
  }
  shared_metrics() = metrics;
  return metrics;
}
void Gui::build_library_tools() {
  library_tools_ = lv_obj_create(launcher_->screen());
  lv_obj_remove_style_all(library_tools_);
  lv_obj_set_pos(library_tools_, 510, 125);
  lv_obj_set_size(library_tools_, 745, 62);
  lv_obj_set_flex_flow(library_tools_, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(library_tools_, 10, 0);
  const char *names[] = {"Todos", "Favoritos", "Recentes", "Buscar",
                         "Favoritar"};
  for (int n = 0; n < 5; ++n) {
    auto *b = lv_button_create(library_tools_);
    lv_obj_set_size(b, n == 4 ? 135 : 130, 46);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x30394c), 0);
    lv_obj_set_user_data(b, reinterpret_cast<void *>(intptr_t(n)));
    auto *label = lv_label_create(b);
    lv_obj_set_style_text_font(label, metrics_.font_small, 0);
    lv_obj_set_width(label,lv_pct(100));
    lv_label_set_long_mode(label,LV_LABEL_LONG_MODE_DOTS);
    lv_label_set_text(label, names[n]);
    lv_obj_center(label);
    if (n == 4)
      favorite_label_ = label;
    lv_obj_add_event_cb(
        b,
        [](lv_event_t *event) {
          auto *self = static_cast<Gui *>(lv_event_get_user_data(event));
          const int action = int(reinterpret_cast<intptr_t>(
              lv_obj_get_user_data(lv_event_get_target_obj(event))));
          if (action < 3)
            self->set_library_view(action);
          else if (action == 3)
            self->open_search();
          else
            self->toggle_favorite();
        },
        LV_EVENT_CLICKED, this);
  }
  refresh_library_tools();
}
void Gui::refresh_library_tools() {
  if (!favorite_label_)
    return;
  const int i = focused_rom_;
  const auto u = AppSettings::get().ui_settings();
  const bool favorite = i >= 0 && size_t(i) < rom_infos_.size() &&
                        std::find(u.favorites.begin(), u.favorites.end(),
                                  rom_infos_[i].rom_path) != u.favorites.end();
  lv_label_set_text(favorite_label_, favorite ? "Desfavoritar" : "Favoritar");
  for (int n = 0; n < 3; ++n) {
    auto *b = lv_obj_get_child(library_tools_, n);
    lv_obj_set_style_bg_color(
        b, lv_color_hex(u.library_view == n ? 0x5d48ad : 0x30394c), 0);
  }
}
void Gui::set_library_view(int view) {
  auto selected = get_selected_rom();
  AppSettings::get().modify_ui([&](UiSettings &u) {
    u.library_view = view;
    u.search.clear();
  });
  refresh_library();
}
void Gui::toggle_favorite() {
  const auto selected = get_selected_rom();
  if (!selected)
    return;
  AppSettings::get().modify_ui([&](UiSettings &u) {
    auto it =
        std::find(u.favorites.begin(), u.favorites.end(), selected->rom_path);
    if (it != u.favorites.end())
      u.favorites.erase(it);
    else if (u.favorites.size() < 128)
      u.favorites.push_back(selected->rom_path);
  });
  refresh_library();
}
void Gui::open_search() {
  if (search_dialog_)
    return;
  launcher_->close_settings();
  launcher_->close_picker();
  auto &board = Tab5Emu::get();
  search_old_test_mode_ = board.keyboard_test_mode();
  board.keyboard_test_mode(true);
  const auto held = board.keyboard_state();
  std::copy(std::begin(held.keys), std::end(held.keys),
            search_previous_keys_.begin());
  search_dialog_ = lv_obj_create(launcher_->screen());
  lv_obj_set_pos(search_dialog_, 240, 170);
  lv_obj_set_size(search_dialog_, 800, 485);
  lv_obj_remove_flag(search_dialog_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(search_dialog_, lv_color_hex(0x171d2a), 0);
  search_text_ = lv_textarea_create(search_dialog_);
  lv_obj_set_pos(search_text_, 10, 10);
  lv_obj_set_size(search_text_, 550, 65);
  lv_textarea_set_one_line(search_text_, true);
  lv_textarea_set_max_length(search_text_, 64);
  lv_textarea_set_placeholder_text(
      search_text_, "Nome ou descrição; Enter aplica, Esc cancela");
  lv_obj_set_style_text_font(search_text_, metrics_.font_body, 0);
  lv_textarea_set_text(search_text_,
                       AppSettings::get().ui_settings().search.c_str());
  auto *apply = lv_button_create(search_dialog_);
  lv_obj_set_pos(apply, 580, 10);
  lv_obj_set_size(apply, 160, 65);
  auto *apply_label = lv_label_create(apply);
  lv_label_set_text(apply_label, "Aplicar");
  lv_obj_set_style_text_font(apply_label, metrics_.font_body, 0);
  lv_obj_center(apply_label);
  lv_obj_add_event_cb(
      apply,
      [](lv_event_t *event) {
        static_cast<Gui *>(lv_event_get_user_data(event))->close_search(true);
      },
      LV_EVENT_CLICKED, this);
  auto *keyboard = lv_keyboard_create(search_dialog_);
  lv_obj_set_align(keyboard, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(keyboard, 10, 90);
  lv_obj_set_size(keyboard, 740, 330);
  lv_keyboard_set_textarea(keyboard, search_text_);
  lv_obj_add_event_cb(
      keyboard,
      [](lv_event_t *event) {
        auto *self = static_cast<Gui *>(lv_event_get_user_data(event));
        if (lv_event_get_code(event) == LV_EVENT_READY)
          self->close_search(true);
        else if (lv_event_get_code(event) == LV_EVENT_CANCEL)
          self->close_search(false);
      },
      LV_EVENT_ALL, this);
}
void Gui::close_search(bool apply) {
  if (!search_dialog_)
    return;
  std::string query = lv_textarea_get_text(search_text_);
  lv_obj_delete(search_dialog_);
  search_dialog_ = search_text_ = nullptr;
  Tab5Emu::get().keyboard_test_mode(search_old_test_mode_);
  if (apply) {
    AppSettings::get().modify_ui([&](UiSettings &u) {
      u.search = query;
      u.library_view = 0;
    });
    refresh_library();
  }
}
void Gui::open_controls(){
  launcher_->close_settings();launcher_->close_picker();
  const auto selected=get_selected_rom();
  const std::string system=selected?console_info(console_of(selected->platform)).key:"snes";
  std::string hash;if(selected){if(auto found=content_hashes_.find(selected->rom_path);found!=content_hashes_.end())hash=found->second;}
  controls_editor_=std::make_unique<ControlsEditor>(system,hash,false);
}
void Gui::read_search_keys() {
  if(controls_editor_){if(!controls_editor_->closed())return;controls_editor_.reset();}
  auto &board = Tab5Emu::get();
  (void)board.gamepad_state();
  const auto state = board.keyboard_state();
  if (board.screen_sleeping() || board.input_wake_blocked()) {
    std::copy(std::begin(state.keys), std::end(state.keys),
              search_previous_keys_.begin());
    shortcut_previous_ = (state.keys[7] >> 3) & 7;
    return;
  }
  if (!search_dialog_) {
    const uint8_t held = (state.keys[7] >> 3) & 7;
    const uint8_t pressed = held & ~shortcut_previous_;
    shortcut_previous_ = held;
    if (!launcher_->settings_open()) {
      if (pressed & 1)
        toggle_favorite();
      else if (pressed & 2)
        open_search();
      else if (pressed & 4)
        set_library_view((AppSettings::get().ui_settings().library_view + 1) %
                         3);
    }
    return;
  }
  bool enter = false, escape = false;
  for (int usage = 4; usage < 90; ++usage) {
    const auto mask = uint8_t(1u << (usage & 7));
    if (!(state.keys[usage >> 3] & mask) ||
        (search_previous_keys_[usage >> 3] & mask))
      continue;
    char value[2]{};
    if (usage >= 4 && usage <= 29)
      value[0] = 'a' + usage - 4;
    else if (usage >= 30 && usage <= 38)
      value[0] = '1' + usage - 30;
    else if (usage == 39)
      value[0] = '0';
    else if (usage == 44)
      value[0] = ' ';
    if (value[0])
      lv_textarea_add_text(search_text_, value);
    else if (usage == 42)
      lv_textarea_delete_char(search_text_);
    else if (usage == 40)
      enter = true;
    else if (usage == 41)
      escape = true;
  }
  std::copy(std::begin(state.keys), std::end(state.keys),
            search_previous_keys_.begin());
  if (escape)
    close_search(false);
  else if (enter)
    close_search(true);
}
void Gui::apply_theme() {
  const auto ui = AppSettings::get().ui_settings();
  Tab5Emu::get().inactivity_seconds(ui.idle_seconds);
  Tab5Emu::get().inactivity_in_game(ui.idle_in_game);
  if (ui.theme == current_theme_)
    return;
  current_theme_ = ui.theme;
  int mode = ui.theme == "light" ? 2 : ui.theme == "dark" ? 1 : 0;
  int strength = 100;
  tab5::content::FileBytes source;
  theme_error_ = "Tema SD ausente ou invalido; usando padrao";
  if (!Tab5Emu::get().usb_msc_host_has_card() &&
      tab5::content::read_file(tab5::content::resolve("assets/themes/" + ui.theme + ".json"),
                               1024, source) == tab5::content::ReadStatus::Ok && source.size()) {
    std::string bytes(reinterpret_cast<const char *>(source.data()), source.size());
    ThemeConfig theme;
    if (parse_theme_json(bytes, theme)) {
      mode = theme.mode; strength = theme.strength;
      theme_error_.clear();
    }
  }
  light_filter_.user_data = reinterpret_cast<void *>(intptr_t(mode));
  light_filter_.filter_cb = [](const lv_color_filter_dsc_t *filter,
                               lv_color_t color, lv_opa_t opacity) {
    const int mode = int(reinterpret_cast<intptr_t>(filter->user_data));
    lv_color_t changed = color;
    if (mode == 2)
      changed =
          lv_color_make(255 - color.red, 255 - color.green, 255 - color.blue);
    else if (mode == 1) {
      const auto gray =
          uint8_t((color.red * 77 + color.green * 150 + color.blue * 29) / 256);
      changed = lv_color_make(gray, gray, gray);
    }
    return lv_color_mix(changed, color, opacity);
  };
  theme_opacity_ = mode ? strength * 255 / 100 : 0;
  if(!external_theme_) {
    auto *display=lv_display_get_default();
    auto *parent=lv_display_get_theme(display);
    external_theme_=std::make_unique<lv_theme_t>(*parent);
    lv_theme_set_parent(external_theme_.get(),parent);
    lv_theme_set_user_data(external_theme_.get(),this);
    lv_theme_set_apply_cb(external_theme_.get(),[](lv_theme_t *theme,lv_obj_t *object){
      static_cast<Gui *>(lv_theme_get_user_data(theme))->apply_color_filter(object);
    });
    lv_display_set_theme(display,external_theme_.get());
  }
  // LVGL color filters are local properties. Reapply to existing widgets;
  // the theme callback also covers widgets created later (search/menus).
  lv_obj_tree_walk(launcher_->screen(),[](lv_obj_t *object,void *context){
    static_cast<Gui *>(context)->apply_color_filter(object);
    return LV_OBJ_TREE_WALK_NEXT;
  },this);
  lv_obj_invalidate(launcher_->screen());
}
void Gui::apply_color_filter(lv_obj_t *object) {
  lv_obj_set_style_color_filter_dsc(object,theme_opacity_?&light_filter_:nullptr,0);
  lv_obj_set_style_color_filter_opa(object,theme_opacity_,0);
}
