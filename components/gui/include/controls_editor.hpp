#pragma once
#include "input_profiles.hpp"
#include "key_edges.hpp"
#include "lvgl.h"
#include <array>
#include <string>
#include <vector>
// Shared touch editor used by HOME and the paused game. Owner holds LVGL mutex.
class ControlsEditor {
public:
  ControlsEditor(std::string system, std::string hash, bool in_game);
  ~ControlsEditor();
  bool closed() const { return dialog_ == nullptr; }
  void update();
  // Dropdown popup lists are siblings on the top layer, not dialog children.
  lv_obj_t *screen() const { return lv_layer_top(); }

private:
  void reload();
  void attach(lv_obj_t *object);
  void apply();
  void close(bool applied = false);
  std::string system_, hash_, previous_;
  bool in_game_, old_test_{false}, reset_{false}, had_file_{false};
  tab5::input::Profile profile_;
  tab5::input::Scope scope_{tab5::input::Scope::System};
  tab5::input::Bindings initial_;
  lv_obj_t *dialog_{nullptr}, *message_{nullptr}, *body_{nullptr};
  lv_obj_t *backdrop_{nullptr};
  lv_group_t *previous_group_{nullptr};
  lv_group_t *group_{nullptr};
  lv_obj_t *scope_dropdown_{nullptr};
  std::array<lv_obj_t *, 3> buttons_{};
  tab5::input::KeyEdges edges_;
  std::array<lv_obj_t *, 15> keys_{};
  std::vector<uint8_t> choices_;
  std::string options_;
};
