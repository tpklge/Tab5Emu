#pragma once
#include <cstdint>
#include <cstring>
#include <span>

namespace tab5::md {
// Compare every record against the running core's own serializer, before
// calling the upstream loader (which otherwise trusts keys and sizes).
inline bool same_records(std::span<const uint8_t> expected,
                         std::span<const uint8_t> incoming) {
  if (expected.empty() || expected.size() != incoming.size()) return false;
  size_t offset = 0;
  while (offset < expected.size()) {
    if (expected.size() - offset < 32 ||
        std::memcmp(expected.data() + offset, incoming.data() + offset, 32))
      return false;
    const auto *p = expected.data() + offset + 28;
    const uint32_t n = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                       (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    offset += 32;
    if (!n || n > expected.size() - offset) return false;
    offset += n;
  }
  return offset == expected.size();
}
inline uint32_t get32(const uint8_t *p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void put32(uint8_t *p, uint32_t n) {
  for (unsigned i = 0; i < 4; ++i) p[i] = n >> (i * 8);
}
} // namespace tab5::md
