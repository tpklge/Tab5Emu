#pragma once
#include <array>
#include <cstdint>
#include "gamepad_state.hpp"

namespace tab5::input {
enum class Profile { Snes, GameBoy, Gba, MegaDrive, MegaDrive6, N64 };
// Physical matrix positions from M5Stack M5Unit-KEYBOARD, Normal mode.
// These are HID identities, independent of text modifiers/repeat.
inline constexpr std::array<uint8_t, 70> matrix_usages = {
  41,30,31,32,33,34,35,36,37,38,39,45,46,76,
  53,30,31,32,33,34,35,36,37,38,39,47,48,49,
  43,20,26,8,21,23,28,24,12,18,19,51,52,42,
  0,225,4,22,7,9,10,11,13,14,15,82,45,40,
  224,226,29,27,6,25,5,17,16,55,80,81,79,44
};
struct Matrix {
  std::array<bool, 70> held{};
  bool event(uint8_t raw) {
    if (raw == 255) return false;
    unsigned row = (raw >> 4) & 7, col = raw & 15;
    if (row >= 5 || col >= 14) return false;
    held[row * 14 + col] = (raw & 128) != 0;
    return true;
  }
  void bitmap(uint8_t (&keys)[32]) const {
    for (auto &byte : keys) byte = 0;
    for (unsigned i = 0; i < held.size(); ++i)
      if (held[i] && matrix_usages[i]) {
        auto usage = matrix_usages[i];
        keys[usage >> 3] |= 1u << (usage & 7);
      }
  }
};
inline GamepadState map(const uint8_t (&keys)[32], Profile profile) {
  auto key = [&](unsigned usage) { return (keys[usage >> 3] >> (usage & 7)) & 1; };
  GamepadState s{};
  s.up = key(8) | key(82);   // E or arrow up
  s.left = key(4) | key(80); // A
  s.down = key(22) | key(81);// S
  s.right = key(7) | key(79);// D
  // Opposite directions neutralize; diagonals remain possible.
  if (s.up && s.down) s.up = s.down = 0;
  if (s.left && s.right) s.left = s.right = 0;
  s.a = key(18); s.b = key(14); // O=A / K=B
  s.x = key(12); s.y = key(13); // I=X / J=Y
  s.start = key(23); s.select = key(28); // T / Y
  s.l = key(24); s.r = key(19); // U / P
  if(profile==Profile::N64){s.l2=key(17);s.r2=key(16);}
  if (profile == Profile::GameBoy || profile == Profile::Gba) s.x = s.y = 0;
  if (profile == Profile::GameBoy) s.l = s.r = 0;
  if (profile == Profile::MegaDrive || profile == Profile::MegaDrive6) {
    // gwenesis' existing adapter consumes b=A, a=B, y=C.
    s.b = key(13); s.a = key(14); s.y = key(18);
    s.x = s.select = s.l = s.r = 0;
    if(profile==Profile::MegaDrive6){s.x=key(12);s.l=key(24);s.r=key(19);s.select=key(28);}
  }
  return s;
}
} // namespace tab5::input
