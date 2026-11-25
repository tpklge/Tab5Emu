#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace tab5::md {
inline bool decode_smd(uint8_t *data, size_t &size) {
  if (!data || size < 512 || data[1] != 3 || data[8] != 0xaa || data[9] != 0xbb)
    return false;
  const size_t payload = size - 512;
  if (!payload || payload > 8 * 1024 * 1024 || payload % 16384)
    return false;
  std::unique_ptr<uint8_t, decltype(&free)> block(
      static_cast<uint8_t *>(malloc(16384)), &free);
  if (!block)
    return false;
  for (size_t off = 0; off < payload; off += 16384) {
    memcpy(block.get(), data + 512 + off, 16384);
    for (size_t i = 0; i < 8192; ++i) {
      data[off + 2 * i] = block.get()[8192 + i];
      data[off + 2 * i + 1] = block.get()[i];
    }
  }
  size = payload;
  return true;
}
} // namespace tab5::md
