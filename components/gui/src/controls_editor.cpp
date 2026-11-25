#include "controls_editor.hpp"
#include "content_storage.hpp"
#include "gui.hpp"
#include "settings.hpp"
#include "tab5-emu.hpp"
#include <algorithm>
#include "file_read.hpp"
ControlsEditor::ControlsEditor(std::string system, std::string hash, bool game)
    : system_(std::move(system)), hash_(std::move(hash)), in_game_(game) {
  system_ = tab5::input::profile_system(system_);
  using tab5::input::Profile;
  profile_ = system_ == "gb" || system_ == "gbc" ? Profile::GameBoy
             : system_ == "gba"                  ? Profile::Gba
             : system_ == "n64"                  ? Profile::N64
             : system_ == "megadrive"
                 ? (AppSettings::get().ui_settings().md_six_buttons
                        ? Profile::MegaDrive6
                        : Profile::MegaDrive)
                 : Profile::Snes;
  if (!Tab5Emu::get().usb_msc_host_has_card()) {
    tab5::content::FileBytes bytes;
    const auto status = tab5::content::read_file(
        tab5::content::resolve("config/input_profiles.json"), 65536, bytes);
    had_file_ = status != tab5::content::ReadStatus::Missing;
    if (status == tab5::content::ReadStatus::Ok && bytes.size())
      previous_.assign(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    else if (had_file_) previous_ = "invalid";
  } else previous_ = "invalid";
  if (previous_.empty())
    previous_ = R"({"schema_version":1,"global":{},"systems":{},"games":{}})";
  for (unsigned u = 0; u < 256; ++u) {
    auto name = tab5::input::key_name(u);
    if (!name.empty()) {
      if (!options_.empty())
        options_ += '\n';
      options_ += name;
      choices_.push_back(u);
    }
  }
  old_test_ = Tab5Emu::get().keyboard_test_mode();
  Tab5Emu::get().keyboard_test_mode(true);
  auto *keypad = Tab5Emu::get().keypad_indev();
  previous_group_ = lv_indev_get_group(keypad);
  group_ = lv_group_create();
  lv_group_set_wrap(group_, true);
  lv_indev_set_group(keypad, group_);
  edges_.update(Tab5Emu::get().keyboard_state().keys);
  // Block touches and gamepad navigation from reaching the underlying UI.
  backdrop_ = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(backdrop_);
  lv_obj_set_size(backdrop_, 1280, 720);
  lv_obj_set_style_bg_color(backdrop_, lv_color_hex(0x10151e), 0);
  lv_obj_set_style_bg_opa(backdrop_, LV_OPA_COVER, 0);
  lv_obj_add_flag(backdrop_, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(backdrop_, LV_OBJ_FLAG_SCROLLABLE);
  dialog_ = lv_obj_create(backdrop_);
  lv_obj_set_pos(dialog_, 165, 50);
  lv_obj_set_size(dialog_, 950, 620);
  lv_obj_remove_flag(dialog_, LV_OBJ_FLAG_SCROLLABLE);
  auto *title = lv_label_create(dialog_);
  lv_obj_set_pos(title, 20, 5);
  lv_label_set_text(title, ("Controles: " + system_).c_str());
  lv_obj_set_style_text_font(dialog_, Gui::shared_metrics().font_small, 0);
  auto *scope = lv_dropdown_create(dialog_);
  scope_dropdown_ = scope;
  attach(scope);
  lv_obj_set_pos(scope, 20, 45);
  lv_obj_set_size(scope, 215, 45);
  lv_dropdown_set_options(scope, "Global\nSistema\nJogo");
  lv_dropdown_set_selected(scope, 1);
  lv_obj_add_event_cb(
      scope,
      [](lv_event_t *e) {
        auto *s = static_cast<ControlsEditor *>(lv_event_get_user_data(e));
        s->scope_ = static_cast<tab5::input::Scope>(
            lv_dropdown_get_selected(lv_event_get_target_obj(e)));
        s->reset_ = false;
        s->reload();
      },
      LV_EVENT_VALUE_CHANGED, this);
  auto *help = lv_label_create(dialog_);
  lv_obj_set_pos(help, 250, 52);
  lv_obj_set_width(help, 630);
  lv_label_set_text(
      help, "Tab: campo | Setas/Enter: escolher | Esc: cancelar. Aplicar grava.");
  body_ = lv_obj_create(dialog_);
  lv_obj_set_pos(body_, 10, 100);
  lv_obj_set_size(body_, 890, 410);
  message_ = lv_label_create(dialog_);
  lv_obj_set_pos(message_, 20, 515);
  lv_obj_set_width(message_, 860);
  const char *labels[] = {"Restaurar camada", "Cancelar", "Aplicar"};
  for (int i = 0; i < 3; ++i) {
    auto *button = lv_button_create(dialog_);
    buttons_[i] = button;
    attach(button);
    lv_obj_set_pos(button, i == 0 ? 20 : i == 1 ? 500 : 700, 550);
    lv_obj_set_size(button, 190, 45);
    lv_obj_set_user_data(button, reinterpret_cast<void *>(intptr_t(i)));
    auto *label = lv_label_create(button);
    lv_label_set_text(label, labels[i]);
    lv_obj_center(label);
    lv_obj_add_event_cb(
        button,
        [](lv_event_t *e) {
          auto *s = static_cast<ControlsEditor *>(lv_event_get_user_data(e));
          int id = int(reinterpret_cast<intptr_t>(
              lv_obj_get_user_data(lv_event_get_target_obj(e))));
          if (id == 0) {
            s->reset_ = true;
            s->reload();
          } else if (id == 1)
            s->close();
          else
            s->apply();
        },
        LV_EVENT_CLICKED, this);
  }
  reload();
}
ControlsEditor::~ControlsEditor() { close(); }
void ControlsEditor::attach(lv_obj_t *object) {
  lv_group_add_obj(group_, object);
  lv_obj_add_event_cb(object, [](lv_event_t *event) {
    if (lv_event_get_key(event) == LV_KEY_ESC)
      static_cast<ControlsEditor *>(lv_event_get_user_data(event))->close();
  }, LV_EVENT_KEY, this);
}
void ControlsEditor::reload() {
  lv_group_remove_all_objs(group_);
  lv_group_add_obj(group_, scope_dropdown_);
  lv_obj_clean(body_);
  keys_.fill(nullptr);
  auto effective = tab5::input::parse_profile(
      previous_, profile_, scope_ == tab5::input::Scope::Global ? "" : system_,
      scope_ == tab5::input::Scope::Game ? hash_ : "");
  if (reset_) {
    const auto preview = tab5::input::edit_profile(previous_, profile_, system_,
                                                   hash_, scope_, {}, 0, true);
    if (preview.valid)
      effective.bindings = preview.bindings;
    else
      effective.valid = false;
  }
  initial_ = effective.bindings;
  lv_label_set_text(
      message_,
      effective.valid
          ? (reset_ ? "Padroes herdados; Aplicar confirma a restauracao"
                    : "Mudancas sao gravadas somente ao Aplicar")
          : "Perfil invalido ou identidade indisponivel; arquivo preservado");
  const auto active = tab5::input::defaults(profile_);
  unsigned row = 0;
  for (unsigned slot = 0; slot < 15; ++slot) {
    if (!active.keys[slot])
      continue;
    int col = row % 2, y = (row / 2) * 48;
    auto *label = lv_label_create(body_);
    lv_obj_set_pos(label, col * 430, y + 10);
    auto name = tab5::input::action_name(profile_, slot);
    lv_label_set_text(label, name.c_str());
    auto *key = lv_dropdown_create(body_);
    attach(key);
    keys_[slot] = key;
    lv_obj_set_pos(key, col * 430 + 105, y);
    lv_obj_set_size(key, 280, 42);
    lv_dropdown_set_options(key, options_.c_str());
    const auto at =
        std::find(choices_.begin(), choices_.end(), initial_.keys[slot]);
    lv_dropdown_set_selected(key,
                             at == choices_.end() ? 0 : at - choices_.begin());
    ++row;
  }
  for (auto *button : buttons_) lv_group_add_obj(group_, button);
  lv_group_focus_obj(scope_dropdown_);
}
void ControlsEditor::update() {
  if (closed()) return;
  const auto state = Tab5Emu::get().keyboard_state();
  const auto pressed = edges_.update(state.keys);
  auto down = [&](unsigned usage) { return pressed[usage >> 3] & (1u << (usage & 7)); };
  if (down(41)) { close(); return; }
  if (down(43)) {
    lv_group_set_editing(group_, false);
    if ((state.keys[225 >> 3] & (1u << (225 & 7))) ||
        (state.keys[229 >> 3] & (1u << (229 & 7)))) lv_group_focus_prev(group_);
    else lv_group_focus_next(group_);
  }
  for (auto entry : {std::pair{40u, LV_KEY_ENTER}, {44u, LV_KEY_ENTER},
                     {79u, LV_KEY_RIGHT}, {80u, LV_KEY_LEFT},
                     {81u, LV_KEY_DOWN}, {82u, LV_KEY_UP}}) {
    if (closed()) return;
    if (down(entry.first)) lv_group_send_data(group_, entry.second);
  }
}
void ControlsEditor::apply() {
  if (Tab5Emu::get().usb_msc_host_has_card()) {
    lv_label_set_text(message_, "Ejete o cartao USB antes de gravar");
    return;
  }
  tab5::content::FileBytes current;
  const auto read = tab5::content::read_file(
      tab5::content::resolve("config/input_profiles.json"), 65536, current);
  const bool missing = read == tab5::content::ReadStatus::Missing;
  if ((read != tab5::content::ReadStatus::Ok && !missing) ||
      missing == had_file_ ||
      (!missing && std::string_view(reinterpret_cast<const char *>(current.data()), current.size()) != previous_)) {
    lv_label_set_text(message_, "Falha de leitura ou arquivo alterado; feche e reabra o editor");
    return;
  }
  auto staged = initial_;
  uint16_t changed = 0;
  for (unsigned i = 0; i < 15; ++i)
    if (keys_[i]) {
      staged.keys[i] = choices_[lv_dropdown_get_selected(keys_[i])];
      if (staged.keys[i] != initial_.keys[i])
        changed |= 1u << i;
    }
  auto result = tab5::input::edit_profile(previous_, profile_, system_, hash_,
                                          scope_, staged, changed, reset_);
  if (!result.valid) {
    lv_label_set_text(message_, result.error.c_str());
    return;
  }
  if (!tab5::content::atomic_write(
          tab5::content::resolve("config/input_profiles.json"),
          result.text.data(), result.text.size())) {
    lv_label_set_text(message_, "Gravacao falhou; geracao anterior preservada");
    return;
  }
  close(true);
}
void ControlsEditor::close(bool applied) {
  if (!dialog_)
    return;
  lv_obj_delete(backdrop_);
  backdrop_ = nullptr;
  dialog_ = nullptr;
  lv_indev_set_group(Tab5Emu::get().keypad_indev(), previous_group_);
  lv_group_delete(group_);
  group_ = nullptr;
  if (applied)
    Tab5Emu::get().keyboard_profile(
        in_game_ ? profile_ : tab5::input::Profile::Snes,
        in_game_ ? system_ : "snes", in_game_ ? hash_ : "");
  Tab5Emu::get().keyboard_test_mode(old_test_);
}
