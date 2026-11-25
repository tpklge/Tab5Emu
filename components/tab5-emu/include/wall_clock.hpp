#pragma once
#include <cstdint>
namespace tab5::content {
// Read the board RTC without resetting or setting it. Zero means untrusted.
uint64_t wall_clock_seconds();
bool set_wall_clock_seconds(uint64_t epoch);
}
