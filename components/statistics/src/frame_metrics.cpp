#include "frame_metrics.hpp"
#include "format.hpp"
#include <atomic>
namespace {
FrameMetrics metrics;
std::atomic<bool> rv32_backend{false};
// Keep formatting scratch out of the USB caller's frame. The dispatcher has
// a 6 KiB stack; these bounded helpers execute one at a time.
__attribute__((noinline)) std::string core_json(const FrameMetricsSnapshot &s) {
  return fmt::format("{{\"backend\":\"{}\",\"nominal_hz\":59.7275,"
    "\"frames_emulated\":{},\"frames_drawn\":{},\"frames_skipped\":{},"
    "\"run_us\":{},\"video_callback_us\":{},\"audio_callback_us\":{},\"pacing_us\":{},"
    "\"audio_requested_bytes\":{},\"audio_accepted_bytes\":{},"
    "\"median_run_us\":{},\"p95_run_us\":{},\"p99_run_us\":{},\"window_samples\":{}}}",
    rv32_backend.load()?"gpsp-rv32":"gpsp-interpreter",s.frames,s.drawn,s.frames-s.drawn,
    s.run_us,s.video_us,s.audio_us,s.pacing_us,s.audio_requested_bytes,s.audio_accepted_bytes,
    s.median_us,s.p95_us,s.p99_us,s.window_samples);
}
__attribute__((noinline)) std::string phase_json(const FrameMetricsSnapshot &s) {
  return fmt::format(",\"execute_us\":{},\"hardware_us\":{},\"ppu_us\":{},\"jit_us\":{},\"rom_page_us\":{},\"rom_page_loads\":{}",
    s.execute_us,s.hardware_us,s.ppu_us,s.jit_us,s.rom_page_us,s.rom_page_loads);
}
__attribute__((noinline)) std::string io_json(const FrameMetricsSnapshot &s) {
  return fmt::format(",\"sd_read_us\":{},\"sd_read_bytes\":{},\"sd_read_calls\":{}}}",
    s.sd_read_us,s.sd_read_bytes,s.sd_read_calls);
}
}
void gba_metrics_backend(bool rv32){rv32_backend=rv32;}
void gba_metrics_reset(){metrics.reset();}
void gba_metrics_record(const FrameSample &s){metrics.record(s);}
FrameMetricsSnapshot gba_metrics_snapshot(){return metrics.snapshot();}
uint64_t gba_metrics_frames(){return metrics.frames();}
std::string gba_metrics_json(){
  const auto s=metrics.snapshot();
  auto result=core_json(s);result.pop_back();result+=phase_json(s);result+=io_json(s);return result;
}
