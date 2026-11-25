#pragma once
#include "keyboard_mapping.hpp"
#include <array>
#include <cstdlib>
#include <span>
namespace tab5::touch {
constexpr int panel_width = 208;
struct Button {
  int x, y, w, h;
  uint16_t mask;
  const char *label;
};
inline constexpr std::array<Button, 9> buttons{
    {{1198, 365, 68, 68, 1, "A"},
     {1082, 417, 68, 68, 2, "B"},
     {1198, 261, 68, 68, 4, "X"},
     {1082, 313, 68, 68, 8, "Y"},
     {28, 570, 152, 58, 16, "SELECT"},
     {1100, 570, 152, 58, 32, "START"},
     {28, 140, 152, 58, 1024, "L"},
     {1100, 140, 152, 58, 2048, "R"},
     {1100, 35, 152, 58, 0, "MENU"}}};
inline constexpr std::array<Button,11> n64_buttons{{
  {1198,440,68,68,1,"A"},{1082,490,68,68,2,"B"},
  {1150,225,68,68,4,"CU"},{1082,300,68,68,8,"CL"},
  {1150,365,62,68,4096,"CD"},{1212,300,64,68,8192,"CR"},
  {28,570,152,58,16,"Z"},{1100,570,152,58,32,"START"},
  {28,140,152,58,1024,"L"},{1100,140,152,58,2048,"R"},
  {1100,35,152,58,0,"MENU"}}};
inline std::span<const Button> buttons_for(input::Profile p){return p==input::Profile::N64?std::span<const Button>(n64_buttons):std::span<const Button>(buttons);}
inline bool active(const Button &b, input::Profile p) {
  if (p == input::Profile::GameBoy)
    return !(b.mask & (4 | 8 | 1024 | 2048));
  if (p == input::Profile::MegaDrive)
    return !(b.mask & (4 | 16 | 1024 | 2048));
  if (p == input::Profile::Gba)
    return !(b.mask & (4 | 8));
  return true;
}
inline bool hit(int x, int y, input::Profile profile, GamepadState &state,
                bool &menu) {
  if (x < 0 || y < 0 || x >= 1280 || y >= 720)
    return false;
  for (const auto &b : buttons_for(profile))
    if (active(b, profile) && x >= b.x && x < b.x + b.w && y >= b.y &&
        y < b.y + b.h) {
      if (!b.mask)
        menu = true;
      else
        state.buttons |= b.mask;
      return true;
    }
  // D-pad includes its four corner areas for two-direction diagonals.
  const int dx = x - 104, dy = y - 370, ax = std::abs(dx), ay = std::abs(dy);
  if (ax <= 86 && ay <= 86) {
    if (ax > 18 && ax * 2 >= ay)
      state.buttons |= dx < 0 ? 256 : 512;
    if (ay > 18 && ay * 2 >= ax)
      state.buttons |= dy < 0 ? 64 : 128;
    return true;
  }
  return false;
}
} // namespace tab5::touch
