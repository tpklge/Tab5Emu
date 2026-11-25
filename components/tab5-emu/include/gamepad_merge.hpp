#pragma once
#include "gamepad_state.hpp"
#include <cstdint>

namespace tab5::input {
struct GamepadInput {
  GamepadState buttons{};
  GamepadAxes axes{};
  bool menu{false};
};
inline void merge_gamepad(GamepadInput &out, const GamepadInput &in) {
  out.buttons.buttons |= in.buttons.buttons;
  out.menu = out.menu || in.menu;
  auto axis = [](int16_t &current, int16_t next) {
    const int32_t a = current, b = next;
    if ((b < 0 ? -b : b) > (a < 0 ? -a : a)) current = next;
  };
  axis(out.axes.lx, in.axes.lx); axis(out.axes.ly, in.axes.ly);
  axis(out.axes.rx, in.axes.rx); axis(out.axes.ry, in.axes.ry);
}
} // namespace tab5::input
