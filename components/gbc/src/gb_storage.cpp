#include "gb_storage.hpp"
#include "battery_store.hpp"
#include "gameboy.hpp"
#include "gb_rtc_math.h"
#include "gb_pause_clock.hpp"
#include "file_read.hpp"
#include "esp_timer.h"
#include "gb_state_layout.hpp"
#include "wall_clock.hpp"
#include <array>
#include <atomic>
#include <filesystem>
#include <vector>
extern "C" {
#include "gnuboy/gnuboy.h"
#include "gnuboy/hw.h"
#include "gnuboy/lcd.h"
#include "gnuboy/mem.h"
#include "gnuboy/rtc.h"
#include "gnuboy/sound.h"
#include "gnuboy/pcm.h"
void tab5_gb_audio_filter_reset(void);
}
namespace {
auto &battery = tab5::content::primary_battery_store();
auto &clock_store = tab5::content::rtc_battery_store();
bool ready = false, has_clock = false;
std::array<int32_t, 8> clock_data{};
std::array<uint32_t, 10> clock_v2{};
std::atomic<uint32_t> emulated_frames{0};
tab5::gb::PauseClock pause_clock;
void advance_pause_clock(bool resume = false) {
  if (!ready || !has_clock) return;
  const auto now = static_cast<uint64_t>(esp_timer_get_time());
  const auto ticks = resume ? pause_clock.end(now, rtc.stop) : pause_clock.poll(now, rtc.stop);
  const auto phase = uint64_t(rtc.t) + ticks % 60;
  rtc.t = phase % 60;
  tab5_gb_rtc_advance(&rtc.d, &rtc.h, &rtc.m, &rtc.s, rtc.stop, &rtc.carry,
                      ticks / 60 + phase / 60);
}
bool valid_clock() {
  return clock_data[0] == 0x31544352 && clock_data[1] >= 0 &&
         clock_data[1] <= 1023 && clock_data[2] >= 0 && clock_data[2] < 24 &&
         clock_data[3] >= 0 && clock_data[3] < 60 && clock_data[4] >= 0 &&
         clock_data[4] < 60 && clock_data[5] >= 0 && clock_data[5] < 60 &&
         (clock_data[6] == 0 || clock_data[6] == 1) &&
         (clock_data[7] == 0 || clock_data[7] == 1);
}
} // namespace
bool initialize_gameboy_storage(std::string_view system,
                                std::string_view hash) {
  release_gameboy_storage();
  emulated_frames = 0;
  if (!ram.sbank || mbc.ramsize < 1 || mbc.ramsize > 16)
    return false;
  if (!battery.configure(system, hash, mbc.batt ? 8192 * mbc.ramsize : 0) ||
      !battery.load(ram.sbank))
    return false;
  has_clock = rtc.batt;
  if (has_clock) {
    clock_v2 = {0x32544352, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (!clock_store.configure(system, hash, sizeof(clock_v2), "rtc-v2.bin"))
      return false;
    bool have_v2 = false;
    std::error_code ec;
    for (const char *suffix : {"", ".bak", ".sha256", ".sha256.bak"}) {
      have_v2 |= std::filesystem::exists(clock_store.path() + suffix, ec);
      if (ec)
        return false;
    }
    if (!have_v2) {
      clock_data = {0x31544352, 0, 0, 0, 0, 0, 0, 0};
      if (!clock_store.configure(system, hash, sizeof(clock_data),
                                 "rtc-v1.bin") ||
          !clock_store.load(clock_data.data()) || !valid_clock())
        return false;
      for (unsigned i = 1; i < 8; ++i)
        clock_v2[i] = clock_data[i];
      // Legacy gnuboy used bit 9 where the cartridge uses bit 8.
      if (clock_v2[1] >= 512)
        clock_v2[1] = (clock_v2[1] & 255) | 256;
      if (!clock_store.configure(system, hash, sizeof(clock_v2), "rtc-v2.bin"))
        return false;
    }
    if (!clock_store.load(clock_v2.data()) || clock_v2[0] != 0x32544352)
      return false;
    clock_data[0] = 0x31544352;
    for (unsigned i = 1; i < 8; ++i)
      clock_data[i] = clock_v2[i];
    if (!valid_clock() || clock_data[1] > 511)
      return false;
    rtc.d = clock_data[1];
    rtc.h = clock_data[2];
    rtc.m = clock_data[3];
    rtc.s = clock_data[4];
    rtc.t = clock_data[5];
    rtc.stop = clock_data[6];
    rtc.carry = clock_data[7];
    const auto saved = uint64_t(clock_v2[8]) | (uint64_t(clock_v2[9]) << 32);
    const auto now = tab5::content::wall_clock_seconds();
    if (saved && now >= saved)
      tab5_gb_rtc_advance(&rtc.d, &rtc.h, &rtc.m, &rtc.s, rtc.stop, &rtc.carry,
                          now - saved);
  }
  ready = true;
  return true;
}
bool gameboy_storage_ready() { return ready; }
bool flush_gameboy_battery() {
  if (!ready)
    return false;
  advance_pause_clock();
  if (!battery.flush(ram.sbank))
    return false;
  if (has_clock) {
    const uint64_t now = tab5::content::wall_clock_seconds();
    clock_v2 = {0x32544352,         uint32_t(rtc.d),     uint32_t(rtc.h),
                uint32_t(rtc.m),    uint32_t(rtc.s),     uint32_t(rtc.t),
                uint32_t(rtc.stop), uint32_t(rtc.carry), uint32_t(now),
                uint32_t(now >> 32)};
    if (!clock_store.flush(clock_v2.data()))
      return false;
  }
  return true;
}
void release_gameboy_storage() {
  battery.reset();
  clock_store.reset();
  ready = has_clock = false;
  pause_clock = {};
}
void pause_gameboy_clock() {
  if (ready && has_clock) pause_clock.begin(esp_timer_get_time());
}
void resume_gameboy_clock() { advance_pause_clock(true); }
bool save_gameboy_checked(std::string_view path) {
  const auto size = tab5::gb::state_bytes(hw.cgb, mbc.ramsize);
  if (!ready || path.empty() || !size)
    return false;
  advance_pause_clock();
  return tab5::content::atomic_file_write(
      std::string(path), size, [&](FILE *file) {
        savestate(file);
        return ftell(file) == static_cast<long>(size);
      });
}
bool load_gameboy_checked(std::string_view path) {
  const auto size = tab5::gb::state_bytes(hw.cgb, mbc.ramsize);
  if (!ready || path.empty() || !size)
    return false;
  tab5::content::FileBytes staged;
  if (!tab5::content::read_exact_file(std::string(path), size, staged) ||
      !tab5::gb::valid_state_header(staged.data(), staged.size(),
                                             hw.cgb, mbc.ramsize))
    return false;
  FILE *memory = fmemopen(staged.data(), staged.size(), "rb");
  if (!memory)
    return false;
  loadstate(memory);
  const bool ok = !ferror(memory);
  const bool closed = fclose(memory) == 0;
  if (!ok || !closed)
    return false;
  // PCM and the high-pass capacitor belong to the discarded timeline, not
  // the restored guest state. The checked loader is also used by USB/resume.
  pcm.pos = 0;
  tab5_gb_audio_filter_reset();
  vram_dirty();
  pal_dirty();
  sound_dirty();
  mem_updatemap();
  // A state restores its RTC timeline. Do not add the pause preceding load
  // to that restored time; later time in this same menu still advances.
  const bool paused = pause_clock.active();
  pause_clock = {};
  if (paused) pause_clock.begin(esp_timer_get_time());
  return true;
}
uint64_t gameboy_frames() {
  return emulated_frames.load(std::memory_order_relaxed);
}
void run_gameboy_tracked() {
  run_gameboy_rom();
  emulated_frames.fetch_add(1, std::memory_order_relaxed);
}
