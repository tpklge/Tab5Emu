#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
namespace tab5::md {
// Linear SRAM only. Serial EEPROM and cartridge-specific mappers need their
// own protocol implementations; refuse them instead of writing a bogus save.
struct LinearSram {
  enum class Kind { None, Linear, Unsupported };
  static constexpr size_t maximum_bytes = 128 * 1024;
  std::array<uint8_t, maximum_bytes> bytes{};
  // Keep the unused cartridge buffer entirely in BSS, with no 128 KiB
  // initialized-data image in flash. configure() establishes the stride.
  uint32_t first{0}, last{0}, stride{0}, control{0};
  size_t size{0};
  static uint32_t be32(const uint8_t *p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
           (uint32_t(p[2]) << 8) | p[3];
  }
  Kind configure(const uint8_t *rom, size_t length) {
    first = last = size = control = 0;
    stride = 1;
    if (!rom || length < 0x200)
      return Kind::Unsupported;
    if (rom[0x1b0] != 'R' || rom[0x1b1] != 'A')
      return Kind::None;
    if (rom[0x1b2] == 0xe8) return Kind::Unsupported;
    first = be32(rom + 0x1b4);
    last = be32(rom + 0x1b8);
    if (first >= last || last >= 0x800000)
      return Kind::Unsupported;
    stride = ((first & 1) == (last & 1)) ? 2 : 1;
    const size_t n = (last - first) / stride + 1;
    if (n > bytes.size())
      return Kind::Unsupported;
    size = n;
    std::fill_n(bytes.data(), size, 0xff);
    control = first >= length ? 1 : 0;
    return Kind::Linear;
  }
  bool contains(uint32_t address) const {
    return size && address >= first && address <= last;
  }
  bool read8(uint32_t address, uint32_t &value) const {
    if (address == 0xa130f1) {
      value = control;
      return true;
    }
    if (!(control & 1) || !contains(address))
      return false;
    value =
        ((address - first) % stride) ? 0xff : bytes[(address - first) / stride];
    return true;
  }
  bool write8(uint32_t address, uint32_t value) {
    if (address == 0xa130f1) {
      control = value & 3;
      return true;
    }
    if (!contains(address))
      return false;
    if ((control & 1) && !(control & 2) && !((address - first) % stride))
      bytes[(address - first) / stride] = value;
    return true;
  }
};
} // namespace tab5::md
