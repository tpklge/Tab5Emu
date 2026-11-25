#pragma once
#include <array>
#include <cstdint>
namespace tab5::input {
struct KeyEdges {
  std::array<uint8_t, 32> previous{};
  std::array<uint8_t, 32> update(const uint8_t (&held)[32]) {
    std::array<uint8_t, 32> pressed{};
    for (unsigned i=0;i<32;++i) {
      pressed[i] = held[i] & ~previous[i]; previous[i] = held[i];
    }
    return pressed;
  }
};
}
