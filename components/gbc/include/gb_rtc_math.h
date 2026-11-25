#pragma once
#include <stdint.h>
// MBC3: 9-bit day counter, sticky carry, halt leaves the subsecond phase
// intact.
static inline void tab5_gb_rtc_advance(int *day, int *hour, int *minute,
                                       int *second, int halted, int *carry,
                                       uint64_t elapsed) {
  if (halted || !elapsed)
    return;
  const uint64_t period = 512ULL * 86400;
  uint64_t now =
      (uint64_t)(*day & 511) * 86400 + *hour * 3600 + *minute * 60 + *second;
  if (elapsed >= period || now + elapsed % period >= period)
    *carry = 1;
  now = (now + elapsed % period) % period;
  *day = now / 86400;
  now %= 86400;
  *hour = now / 3600;
  now %= 3600;
  *minute = now / 60;
  *second = now % 60;
}
