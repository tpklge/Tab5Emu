#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace tab5::audio {
// Preserve the first 50 ms of stereo PCM, then submit it as a single batch.
// The producer owns this state. Reset at every audio timeline discontinuity;
// callers must keep the source clock unchanged while collecting the preroll.
class Preroll {
public:
  void reset(uint32_t rate) {
    target_ =
        std::clamp<size_t>((uint64_t(rate) * 50 + 999) / 1000, 1, capacity);
    buffered_ = 0;
    primed_ = false;
  }
  template <class Submit>
  void push(const int16_t *pcm, size_t frames, Submit submit) {
    if (!pcm || !frames)
      return;
    if (!primed_) {
      const size_t take = std::min(frames, target_ - buffered_);
      std::memcpy(pcm_.data() + buffered_ * 2, pcm, take * 4);
      buffered_ += take;
      pcm += take * 2;
      frames -= take;
      if (buffered_ < target_)
        return;
      submit(pcm_.data(), buffered_);
      primed_ = true;
      buffered_ = 0;
    }
    if (frames)
      submit(pcm, frames);
  }

private:
  static constexpr size_t capacity = 2048;
  std::array<int16_t, capacity * 2> pcm_{};
  size_t buffered_ = 0, target_ = 1639;
  bool primed_ = false;
};
} // namespace tab5::audio
