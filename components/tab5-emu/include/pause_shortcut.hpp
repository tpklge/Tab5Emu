#pragma once
#include <cstdint>
namespace tab5::input {
// The guest's Start/Select remain immediate; only the frontend chord waits.
class PauseShortcut {
public:
  bool update(bool start, bool select, uint64_t now_us) {
    if (!start && !select) { armed_=true; started_=false; }
    if (!start || !select) { started_=false; return false; }
    if (!armed_) return false;
    if (!started_) { since_=now_us; started_=true; }
    if (now_us-since_ < 450000) return false;
    armed_=false; started_=false; return true;
  }
private:
  uint64_t since_{0};
  bool armed_{true}, started_{false};
};
}
