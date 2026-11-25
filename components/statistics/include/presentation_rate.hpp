#pragma once
#include <cstdint>

namespace tab5 {
// Counts completed images over wall time. Rendering cost alone omits the
// time spent executing the game, pacing and waiting for the next image.
class PresentationRate {
public:
  void reset(bool clear = true) {
    started_ = false;
    frames_ = 0;
    if (clear) fps_ = 0;
  }
  bool present(uint64_t now) {
    if (!started_ || now < since_) {
      started_ = true;
      since_ = now;
      frames_ = 0;
      return false;
    }
    ++frames_;
    const auto elapsed = now - since_;
    if (elapsed < 1000000) return false;
    fps_ = frames_ * 1000000.0f / elapsed;
    since_ = now;
    frames_ = 0;
    return true;
  }
  float fps() const { return fps_; }
private:
  bool started_ = false;
  uint64_t since_ = 0;
  uint32_t frames_ = 0;
  float fps_ = 0;
};
}
