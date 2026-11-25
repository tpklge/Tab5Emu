#include "tab5-emu.hpp"
#include <algorithm>
#include <cstring>

void Tab5Emu::keyboard_profile(tab5::input::Profile p, std::string_view system, std::string_view hash) {
  // SD access belongs to the main task, never the polling/USB dispatch task.
  const auto selected=tab5::input::load_profile(p,system,hash);
  std::lock_guard<std::mutex> lock(hid_mutex_);
  keyboard_bindings_=selected.bindings;
  // A remote quit reports menu before main finishes resuming the GUI.
  // Preserve an already enabled test mode across that HOME transition.
  if (!hash.empty()) keyboard_test_mode_=false;
  keyboard_profile_note_=selected.valid ? (selected.from_file ? "perfil SD ativo" : "padroes") : selected.error;
  KeyboardState held=keyboard_;
  for (unsigned i=0;i<32;++i) held.keys[i]|=i2c_keyboard_.keys[i];
  keyboard_gate_.block(held.keys);
  press_latch_=0;
  menu_requested_=false;
}

std::string Tab5Emu::keyboard_profile_status() const {
  std::lock_guard<std::mutex> lock(hid_mutex_);
  return keyboard_profile_note_;
}

void Tab5Emu::keyboard_test_mode(bool on) {
  std::lock_guard<std::mutex> lock(hid_mutex_);
  keyboard_test_mode_=on;
  auto held=keyboard_;
  for (unsigned i=0;i<32;++i) held.keys[i]|=i2c_keyboard_.keys[i];
  keyboard_gate_.block(held.keys);
  press_latch_=0;
  menu_requested_=false;
}

Tab5Emu::KeyboardState Tab5Emu::action_keyboard_locked() const {
  auto state=keyboard_;
  for (unsigned i=0;i<32;++i) state.keys[i]|=i2c_keyboard_.keys[i];
  keyboard_gate_.filter(state.keys);
  return state;
}

Tab5Emu::KeyboardState Tab5Emu::action_keyboard_state() const {
  std::lock_guard<std::mutex> lock(hid_mutex_);
  return action_keyboard_locked();
}

GamepadState Tab5Emu::keyboard_gamepad_state() const {
  std::lock_guard<std::mutex> lock(hid_mutex_);
  if (keyboard_test_mode_) return {};
  const auto keys=action_keyboard_locked();
  return tab5::input::map(keys.keys,keyboard_bindings_);
}

void Tab5Emu::publish_keyboard_locked(bool i2c,const KeyboardState &report) {
  const auto &raw_before=i2c?i2c_keyboard_:keyboard_;
  if(memcmp(raw_before.keys,report.keys,32)!=0 || std::any_of(std::begin(report.keys),std::end(report.keys),[](uint8_t v){return v!=0;}))input_activity();
  const auto before=action_keyboard_locked();
  if (i2c) i2c_keyboard_=report; else keyboard_=report;
  KeyboardState held=keyboard_;
  for (unsigned i=0;i<32;++i) held.keys[i]|=i2c_keyboard_.keys[i];
  keyboard_gate_.release(held.keys);
  if(screen_sleeping_ || wake_block_){keyboard_gate_.block(held.keys);return;}
  if (keyboard_test_mode_) return; // raw diagnostics only; touch stays active
  const auto after=action_keyboard_locked();
  const auto old=tab5::input::map(before.keys,keyboard_bindings_);
  const auto now=tab5::input::map(after.keys,keyboard_bindings_);
  press_latch_.fetch_or(now.buttons & ~old.buttons);
  if (tab5::input::menu_pressed(after.keys,keyboard_bindings_) &&
      !tab5::input::menu_pressed(before.keys,keyboard_bindings_)) menu_requested_=true;
}
