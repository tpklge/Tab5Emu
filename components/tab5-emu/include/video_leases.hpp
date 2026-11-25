#pragma once
#include <array>
#include <cstdint>
namespace tab5::video {
// Caller serializes this book with queue push/pop. A queued pointer stays
// pinned while the consumer transfers it to its local frame and renders it.
class Leases {
public:
  bool available(const void *p) const {
    for(const auto &s:slots_)if(s.pointer==p && s.count)return false;
    return true;
  }
  bool retain(const void *p) {
    if(!p)return true;
    for(auto &s:slots_)if(s.pointer==p){++s.count;return true;}
    for(auto &s:slots_)if(!s.count){s={p,1};return true;}
    return false;
  }
  void release(const void *p) {
    if(!p)return;
    for(auto &s:slots_)if(s.pointer==p && s.count){if(!--s.count)s.pointer=nullptr;return;}
  }
  bool idle() const {for(const auto &s:slots_)if(s.count)return false;return true;}
private:
  struct Slot{const void *pointer{nullptr};uint32_t count{0};};
  std::array<Slot,3> slots_{};
};
}
