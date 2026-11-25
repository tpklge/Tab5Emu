// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
// gpSP keeps translation tags in the other half of each shared RAM buffer.
// Inspect whole halfword tags, including byte writes and mirrored addresses.
static inline int tab5_rv32_ram_has_code(uint32_t address, unsigned width,
                                       const uint8_t *ewram_tags,
                                       const uint8_t *iwram_tags) {
  const uint8_t *tags;
  uint32_t offset;
  if ((address >> 24) == 2) {
    tags = ewram_tags;
    offset = address & 0x3ffff;
  } else if ((address >> 24) == 3) {
    tags = iwram_tags;
    offset = address & 0x7fff;
  } else return 0;
  offset &= ~(uint32_t)((width == 4 ? 4 : 2) - 1);
  for (unsigned i = 0; i < (width == 4 ? 4 : 2); ++i)
    if (tags[offset + i]) return 1;
  return 0;
}
