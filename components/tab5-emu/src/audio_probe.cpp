#include "audio_probe.hpp"
#include "driver/i2s_common.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include <atomic>
#include <cstring>
namespace {
std::atomic<bool> running{false};
std::atomic<unsigned> warmup{6};
std::atomic<uint64_t> blocks{0}, partial{0}, empty{0}, missing{0}, max_gap{0},
    errors{0}, last_write{0}, late{0}, late_frames{0};
std::atomic<uint64_t> startup_blocks{0},startup_partial{0},startup_empty{0},startup_missing{0},startup_errors{0};
std::atomic<size_t> received{0};
bool output_task() {
  return std::strcmp(pcTaskGetName(nullptr), "tab5_audio out") == 0;
}
} // namespace
namespace tab5::audio_probe {
void expected(bool active) {
  const bool previous = running.exchange(active);
  if (active && !previous) {
    warmup = 6;
    last_write = 0;
  }
}
void reset() {
  running = false;
  blocks = 0;
  partial = 0;
  empty = 0;
  missing = 0;
  max_gap = 0;
  errors = 0;
  last_write = 0;
  received = 0;
  late=0;late_frames=0;
  warmup = 6;
  startup_blocks=0;startup_partial=0;startup_empty=0;startup_missing=0;startup_errors=0;
}
Snapshot snapshot() {
  return {blocks.load(),  partial.load(), empty.load(),
          missing.load(), max_gap.load(), errors.load(),late.load(),late_frames.load(),startup_blocks.load(),startup_partial.load(),startup_empty.load(),startup_missing.load(),startup_errors.load()};
}
} // namespace tab5::audio_probe
extern "C" size_t __real_xStreamBufferReceive(StreamBufferHandle_t, void *,
                                              size_t, TickType_t);
extern "C" size_t __wrap_xStreamBufferReceive(StreamBufferHandle_t stream,
                                              void *buffer, size_t n,
                                              TickType_t timeout) {
  const size_t got = __real_xStreamBufferReceive(stream, buffer, n, timeout);
  if (output_task())
    received = got;
  return got;
}
extern "C" esp_err_t __real_i2s_channel_write(i2s_chan_handle_t, const void *,
                                              size_t, size_t *, uint32_t);
extern "C" esp_err_t __wrap_i2s_channel_write(i2s_chan_handle_t channel,
                                              const void *buffer, size_t n,
                                              size_t *written,
                                              uint32_t timeout) {
  const bool output = output_task();
  const auto now = uint64_t(esp_timer_get_time());
  const size_t valid = output ? received.exchange(0) : 0;
  const auto result =
      __real_i2s_channel_write(channel, buffer, n, written, timeout);
  if (output && running) {
    if (warmup.load()) {
      --warmup;
      ++startup_blocks;
      if (result != ESP_OK) ++startup_errors;
      if (valid < n) {
        if (valid) ++startup_partial;
        else ++startup_empty;
        startup_missing += (n - valid) / 4;
      }
    }
    else {
      const auto previous = last_write.exchange(now);
      if (previous && now - previous > max_gap)
        max_gap = now - previous;
      const uint64_t period=n*1000000ull/(48000*4);
      if(previous && now-previous>period+2000){++late;late_frames+=(now-previous-period)*48000/1000000;}
      ++blocks;
      if (result != ESP_OK)
        ++errors;
      if (valid < n) {
        if (valid)
          ++partial;
        else
          ++empty;
        missing += (n - valid) / 4;
      }
    }
  }
  return result;
}
