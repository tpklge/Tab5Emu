#pragma once
#include <cstdint>
namespace tab5::gb {
// Monotonic time only: changing the board date cannot jump a live cart clock.
class PauseClock {
public:
  void begin(uint64_t now) { if (!active_) { active_ = true; last_ = now; } }
  uint64_t poll(uint64_t now, bool halted) {
    if (!active_) return 0;
    const uint64_t elapsed = now >= last_ ? now - last_ : 0;
    last_ = now;
    if (halted) { fraction_ = 0; return 0; }
    const uint64_t fractional = (elapsed % 1000000) * 60 + fraction_;
    fraction_ = fractional % 1000000;
    return (elapsed / 1000000) * 60 + fractional / 1000000;
  }
  uint64_t end(uint64_t now, bool halted) {
    const auto ticks = poll(now, halted); active_ = false; return ticks;
  }
  bool active() const { return active_; }
private:
  uint64_t last_{0}, fraction_{0};
  bool active_{false};
};
} // namespace tab5::gb
