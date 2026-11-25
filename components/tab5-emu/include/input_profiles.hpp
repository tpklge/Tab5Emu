#pragma once
#include "keyboard_mapping.hpp"
#include <string>
#include <string_view>

namespace tab5::input {
inline std::string_view profile_system(std::string_view system) {
  return system == "genesis" ? std::string_view("megadrive") : system;
}
// Slots 0..13 use GamepadState::Button; slot 14 is the frontend menu.
struct Bindings {
  std::array<uint8_t, 15> keys{};
};
Bindings defaults(Profile profile);
GamepadState map(const uint8_t (&keys)[32], const Bindings &bindings);
bool menu_pressed(const uint8_t (&keys)[32], const Bindings &bindings);
struct ProfileSelection {
  Bindings bindings;
  bool valid{true};
  bool from_file{false};
  std::string error;
};
// Invalid files fall back transactionally to defaults, never a partial map.
ProfileSelection parse_profile(std::string_view json, Profile profile,
                               std::string_view system, std::string_view rom_sha256 = {});
ProfileSelection load_profile(Profile profile, std::string_view system,
                              std::string_view rom_sha256 = {});

enum class Scope { Global, System, Game };
std::string key_name(uint8_t usage);
int key_usage(std::string_view name);
std::string action_name(Profile p,unsigned slot);
struct EditResult {bool valid{false};std::string text,error;Bindings bindings;};
EditResult edit_profile(std::string_view previous,Profile p,std::string_view system,
                        std::string_view hash,Scope scope,const Bindings &bindings,
                        uint16_t changed,bool reset=false);

// Held keys at a focus/profile transition remain blocked until released.
// Caller owns synchronization. Raw diagnostics are kept separately.
struct KeyGate {
  uint8_t blocked[32]{};
  void block(const uint8_t (&held)[32]) {
    for (unsigned i=0;i<32;++i) blocked[i] = held[i];
  }
  void release(const uint8_t (&held)[32]) {
    for (unsigned i=0;i<32;++i) blocked[i] &= held[i];
  }
  void filter(uint8_t (&held)[32]) const {
    for (unsigned i=0;i<32;++i) held[i] &= ~blocked[i];
  }
};
} // namespace tab5::input
