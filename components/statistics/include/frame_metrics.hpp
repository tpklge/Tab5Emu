#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>

struct FrameSample {
  uint32_t run_us{0}, video_us{0}, audio_us{0}, pacing_us{0};
  uint32_t sd_read_us{0}, sd_read_bytes{0}, sd_read_calls{0};
  uint32_t execute_us{0}, hardware_us{0}, ppu_us{0}, jit_us{0}, rom_page_us{0},
      rom_page_loads{0};
  uint32_t audio_requested_bytes{0}, audio_accepted_bytes{0};
  bool drawn{false};
};
struct FrameMetricsSnapshot {
  uint64_t frames{0}, drawn{0}, run_us{0}, video_us{0}, audio_us{0},
      pacing_us{0};
  uint64_t sd_read_us{0}, sd_read_bytes{0}, sd_read_calls{0};
  uint64_t execute_us{0}, hardware_us{0}, ppu_us{0}, jit_us{0}, rom_page_us{0},
      rom_page_loads{0};
  uint64_t audio_requested_bytes{0}, audio_accepted_bytes{0};
  uint32_t median_us{0}, p95_us{0}, p99_us{0}, window_samples{0};
};
// Bounded rolling timing distribution. Totals include all frames since reset;
// quantiles are nearest-rank over the most recent 1024 run() durations.
class FrameMetrics {
public:
  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    totals_ = {};
    size_ = cursor_ = 0;
  }
  void record(const FrameSample &sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++totals_.frames;
    totals_.drawn += sample.drawn;
    totals_.run_us += sample.run_us;
    totals_.video_us += sample.video_us;
    totals_.audio_us += sample.audio_us;
    totals_.pacing_us += sample.pacing_us;
    totals_.sd_read_us += sample.sd_read_us;
    totals_.sd_read_bytes += sample.sd_read_bytes;
    totals_.sd_read_calls += sample.sd_read_calls;
    totals_.execute_us += sample.execute_us;
    totals_.hardware_us += sample.hardware_us;
    totals_.ppu_us += sample.ppu_us;
    totals_.jit_us += sample.jit_us;
    totals_.rom_page_us += sample.rom_page_us;
    totals_.rom_page_loads += sample.rom_page_loads;
    totals_.audio_requested_bytes += sample.audio_requested_bytes;
    totals_.audio_accepted_bytes += sample.audio_accepted_bytes;
    samples_[cursor_] = sample.run_us;
    cursor_ = (cursor_ + 1) % samples_.size();
    size_ = std::min(size_ + 1, samples_.size());
  }
  FrameMetricsSnapshot snapshot() const {
    // USB dispatcher has a 6 KiB stack. Keep the 4 KiB sorting scratch in
    // owned storage, never on the caller's stack; serialize readers only.
    std::lock_guard<std::mutex> reader(scratch_mutex_);
    FrameMetricsSnapshot out;
    size_t size;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      out = totals_;
      size = size_;
      std::copy_n(samples_.begin(), size, scratch_.begin());
    }
    out.window_samples = size;
    if (size) {
      std::sort(scratch_.begin(), scratch_.begin() + size);
      out.median_us = scratch_[(size * 50 + 99) / 100 - 1];
      out.p95_us = scratch_[(size * 95 + 99) / 100 - 1];
      out.p99_us = scratch_[(size * 99 + 99) / 100 - 1];
    }
    return out;
  }
  uint64_t frames() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return totals_.frames;
  }

private:
  mutable std::mutex mutex_;
  mutable std::mutex scratch_mutex_;
  mutable std::array<uint32_t, 1024> scratch_{};
  std::array<uint32_t, 1024> samples_{};
  size_t size_{0}, cursor_{0};
  FrameMetricsSnapshot totals_{};
};

// Implemented outside core overlays, so diagnostics can read them at any time.
void gba_metrics_reset();
void gba_metrics_backend(bool rv32);
void gba_metrics_record(const FrameSample &sample);
FrameMetricsSnapshot gba_metrics_snapshot();
uint64_t gba_metrics_frames();
std::string gba_metrics_json();
