// GBA core glue: a minimal libretro host for gpSP's front end.
#include "gba.hpp"
#include "shared_memory.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include "file_read.hpp"
#include <string>
#include <thread>
#include <vector>

#include <esp_heap_caps.h>
#include <esp_timer.h>

#include "battery_store.hpp"
#include "render_budget.hpp"
#include "audio_preroll.hpp"
#include "box-emu.hpp"
#include "content_paths.hpp"
#include "content_storage.hpp"
#include "frame_metrics.hpp"
#include "settings.hpp"
#include "statistics.hpp"

#include <algorithm>
#include <sdkconfig.h>
#if CONFIG_JK_ESP_DEBUG
// the JK component's sampling profiler (tick hook on core 0); weak so this
// component does not depend on it
extern "C" void jk_prof_start(void) __attribute__((weak));
extern "C" void jk_prof_dump(int top) __attribute__((weak));
#endif

#include <libretro.h>
#include <streams/file_stream.h>

namespace {
void check_heap(const char *stage) {
#ifdef TAB5_GBA_HEAP_CHECK
  if (!heap_caps_check_integrity_all(true)) {
    fmt::print("gba: heap corruption at {}\n", stage);
    abort();
  }
#else
  (void)stage;
#endif
}
bool g_initialized = false;
FrameSample g_sample;
auto &g_battery = tab5::content::primary_battery_store();
bool g_storage_ready = false;
#ifdef TAB5_GBA_RV32
bool g_force_interpreter = false;
#endif
// rendered vs skipped frames (the frame-time statistics blend both)
uint64_t g_rendered_frames = 0, g_rendered_us = 0, g_skipped_frames = 0,
         g_skipped_us = 0;
bool g_frame_rendered = false;
tab5::video::RenderBudget g_render_budget;
uint64_t g_next_due = 0; // cumulative frame deadline (esp_timer us)
uint32_t g_audio_rate = 32768;
tab5::audio::Preroll g_audio_preroll;
uint32_t g_audio_generation = 0, g_preroll_rate = 0;
int g_frame_index = 0;
uint8_t *g_last_frame = nullptr;
retro_audio_buffer_status_callback_t g_audio_status_cb = nullptr;
uint64_t g_last_elapsed = 0;
float g_fps = 59.7275f;
int g_skip_run =
    0; // consecutive skipped frames (bounded by the core's FRAMESKIP_MAX)

constexpr size_t GBA_W = 240, GBA_H = 160;

// core options the host answers
struct Option {
  const char *key;
  const char *value;
};
const Option OPTIONS[] = {
    {"gpsp_frameskip", "auto"}, // skip when the host reports an audio underrun
    {"gpsp_sound_rate", "32768"}, // native-ish bandwidth, half the mixing work
#ifdef TAB5_GBA_RV32
    {"gpsp_drc", "enabled"},
#else
    {"gpsp_drc", "disabled"},
#endif
    {"gpsp_bios", "auto"}, // gba_bios.bin next to the ROM if present
    {"gpsp_boot_mode", "game"},
    {"gpsp_color_correction", "disabled"},
    {"gpsp_frame_mixing", "disabled"},
    {"gpsp_sprlim", "disabled"},
    {"gpsp_rtc", "auto"},
    {"gpsp_rumble", "disabled"},
    {"gpsp_serial", "auto"},
    {"gpsp_turbo_period", "4"},
    {"gpsp_frameskip_threshold", "33"},
    {"gpsp_frameskip_interval", "0"},
};

void log_cb(enum retro_log_level level, const char *fmt, ...) {
  if (level < RETRO_LOG_INFO)
    return;
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  fmt::print("[gpsp] {}", buf);
}

bool environ_cb(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
    static const std::string bios = tab5::content::resolve("bios");
    *static_cast<const char **>(data) = bios.c_str();
    return true;
  }
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    return *static_cast<enum retro_pixel_format *>(data) ==
           RETRO_PIXEL_FORMAT_RGB565;
  case RETRO_ENVIRONMENT_GET_VARIABLE: {
    auto *var = static_cast<struct retro_variable *>(data);
#ifdef TAB5_GBA_RV32
    if (strcmp(var->key, "gpsp_drc") == 0) {
      var->value =
          AppSettings::get().ui_settings().gba_cpu && !g_force_interpreter
              ? "enabled" : "disabled";
      return true;
    }
#endif
    for (const auto &o : OPTIONS) {
      if (strcmp(o.key, var->key) == 0) {
        var->value = o.value;
        return true;
      }
    }
    var->value = nullptr;
    return false;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
    *static_cast<bool *>(data) = false;
    return true;
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
    static_cast<struct retro_log_callback *>(data)->log = &log_cb;
    return true;
  case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    return true;
  case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
    auto *cb =
        static_cast<const struct retro_audio_buffer_status_callback *>(data);
    g_audio_status_cb = cb ? cb->callback : nullptr;
    return true;
  }
  case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
    auto *av = static_cast<const struct retro_system_av_info *>(data);
    g_fps = av->timing.fps;
    BoxEmu::get().audio_sample_rate(
        static_cast<uint32_t>(av->timing.sample_rate));
    return true;
  }
  case RETRO_ENVIRONMENT_SET_MESSAGE:
    fmt::print("[gpsp] {}\n",
               static_cast<const struct retro_message *>(data)->msg);
    return true;
  case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
    fmt::print("[gpsp] {}\n",
               static_cast<const struct retro_message_ext *>(data)->msg);
    return true;
  case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION:
    *static_cast<unsigned *>(data) = 1;
    return true;
  default:
    return false; // system dir, VFS, perf, rumble, memory maps, netpacket, ...
  }
}

void video_cb(const void *data, unsigned width, unsigned height, size_t pitch) {
  if (!data)
    return; // skipped frame
  const auto started = esp_timer_get_time();
  auto &emu = BoxEmu::get();
  uint8_t *dst = g_frame_index ? emu.frame_buffer1() : emu.frame_buffer0();
  if (!dst || !emu.video_frame_available(dst))
    return;
  g_frame_rendered = true;
  const size_t row = width * 2;
  if (pitch == row) {
    memcpy(dst, data, row * height);
  } else {
    for (unsigned y = 0; y < height; y++) {
      memcpy(dst + y * row, static_cast<const uint8_t *>(data) + y * pitch,
             row);
    }
  }
  g_last_frame = dst;
  emu.push_frame(dst);
  g_frame_index ^= 1;
  g_sample.video_us += esp_timer_get_time() - started;
}

size_t audio_batch_cb(const int16_t *data, size_t frames) {
  const auto started = esp_timer_get_time();
  auto &emu = BoxEmu::get();
  const auto rate = emu.audio_sample_rate();
  if (g_audio_generation != emu.audio_generation() || g_preroll_rate != rate || emu.is_muted()) {
    g_audio_generation = emu.audio_generation();
    g_preroll_rate = rate;
    g_audio_preroll.reset(rate);
  }
  if (!emu.is_muted()) {
    g_audio_preroll.push(data, frames, [&](const int16_t *pcm, size_t count) {
      g_sample.audio_requested_bytes += count * 4;
      g_sample.audio_accepted_bytes += emu.play_audio(
          reinterpret_cast<const uint8_t *>(pcm), count * 4);
    });
  }
  g_sample.audio_us += esp_timer_get_time() - started;
  return frames;
}

void input_poll_cb() {}

int16_t input_state_cb(unsigned port, unsigned device, unsigned index,
                       unsigned id) {
  if (port != 0 || device != RETRO_DEVICE_JOYPAD)
    return 0;
  const auto st = BoxEmu::get().gamepad_state();
  // shoulders: the gamepad's / keyboard's L and R, or the touch pad's X / Y
  const bool l = st.l || st.x, r = st.r || st.y;
  int16_t mask = 0;
  if (st.b)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_B;
  if (st.a)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_A;
  if (st.select)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT;
  if (st.start)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_START;
  if (st.up)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_UP;
  if (st.down)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN;
  if (st.left)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT;
  if (st.right)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT;
  if (l)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_L;
  if (r)
    mask |= 1 << RETRO_DEVICE_ID_JOYPAD_R;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
    return mask;
  return (mask >> id) & 1;
}
} // namespace

extern "C" {
extern uint8_t *ewram, *iwram, *vram, *gamepak_backup;
extern int32_t *sound_buffer;
extern uint8_t (*obj_priority_list)[160][128];
size_t gpsp_sound_buffer_bytes(void);
size_t gpsp_obj_priority_bytes(void);
}
// The ROM cache's 1 MB blocks (gpSP init_gamepak_buffer): slices of the
// HAL's ROM arena first, then the heap. The arena stays reserved at its boot
// address for the carts that keep address-bound save states in it (MGS,
// TPM); releasing it for the cache let it come back somewhere else.
// The core asks for blocks until it has ROM_BUFFER_SIZE or one fails, so the
// heap ones stop short of the last PSRAM_RESERVE bytes: the frame buffers,
// the pause menu's screenshots and a save state's buffer come after it.
namespace {
uint32_t g_arena_slices = 0; // bit i: slice i of the ROM arena is a cache block
constexpr size_t PSRAM_RESERVE = 2 * 1024 * 1024;
} // namespace
extern "C" void *gpsp_rom_block_alloc(size_t bytes) {
  auto &emu = Tab5Emu::get();
  if (auto *arena = emu.rom_arena(emu.rom_arena_size())) {
    const size_t n = std::min<size_t>(emu.rom_arena_size() / bytes, 32);
    for (size_t i = 0; i < n; i++) {
      if (!(g_arena_slices & (1u << i))) {
        g_arena_slices |= 1u << i;
        return arena + i * bytes;
      }
    }
  }
  if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) < bytes + PSRAM_RESERVE) {
    return nullptr; // a smaller cache pages the rest of the ROM from the card
  }
  return malloc(bytes);
}
extern "C" void gpsp_rom_block_free(void *p) {
  auto &emu = Tab5Emu::get();
  const auto *arena = emu.rom_arena(emu.rom_arena_size());
  const auto *b = static_cast<const uint8_t *>(p);
  if (arena && b >= arena && b < arena + emu.rom_arena_size()) {
    g_arena_slices &= ~(1u << ((b - arena) / (1024 * 1024)));
    return;
  }
  free(p);
}

// the core's big RAM arrays, from the shared pool the emulators hand around
// (components/shared_memory); zeroed, as the statics they replace were
static bool gba_shared_alloc() {
  struct Item {
    void **p;
    size_t bytes;
    const char *name;
  };
  const Item items[] = {
      {reinterpret_cast<void **>(&ewram), 1024 * 256 * 2, "ewram"},
      {reinterpret_cast<void **>(&iwram), 1024 * 32 * 2, "iwram"},
      {reinterpret_cast<void **>(&vram), 1024 * 96, "vram"},
      {reinterpret_cast<void **>(&gamepak_backup), 1024 * 128,
       "gamepak_backup"},
      {reinterpret_cast<void **>(&sound_buffer), gpsp_sound_buffer_bytes(),
       "sound_buffer"},
      {reinterpret_cast<void **>(&obj_priority_list), gpsp_obj_priority_bytes(),
       "obj_priority_list"},
  };
  for (const auto &it : items) {
    const bool hot_iwram=it.p==reinterpret_cast<void **>(&iwram);
    const bool internal_room=hot_iwram && heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)>=it.bytes+64*1024;
    shared_mem_request_t req{.size = it.bytes,
                             .region = SHARED_MEM_CACHE_LINE,
                             .storage = internal_room?SHARED_MEM_INTERNAL:SHARED_MEM_PSRAM};
    *it.p = shared_mem_allocate(&req);
    const bool internal=internal_room && *it.p;
    if(internal_room && !*it.p){req.storage=SHARED_MEM_PSRAM;*it.p=shared_mem_allocate(&req);}
    if(hot_iwram)fmt::print("gba: IWRAM backing {} bytes, internal={}\n",it.bytes,internal);
    if (!*it.p) {
      fmt::print("gba: no shared memory for {} ({} bytes)\n", it.name,
                 it.bytes);
      return false;
    }
    memset(*it.p, 0, it.bytes);
  }
  return true;
}
static void gba_shared_free() {
  shared_mem_clear();
  ewram = iwram = vram = gamepak_backup = nullptr;
  sound_buffer = nullptr;
  obj_priority_list = nullptr;
}

void init_gba(const std::string &rom_path) {
  check_heap("before init");
  g_initialized = false;
  g_storage_ready = false;
  g_preroll_rate = 0;
  const auto hash = tab5::content::rom_sha256(rom_path);
#ifdef TAB5_GBA_RV32
  // Circle (USA): RV32 fails during its IRQ startup, including a fresh boot.
  // The interpreter handles this ROM and reaches nominal speed in gameplay.
  // Match verified content, so renamed files retain the compatibility policy.
  g_force_interpreter = hash ==
      "ad9fabaf4398e0f4a2a095bacddc6c0dc7e139903b41e088ca3590c44b684da0";
  if (g_force_interpreter && AppSettings::get().ui_settings().gba_cpu)
    fmt::print("gba: Circle IRQ compatibility: using interpreter\n");
#endif
  g_battery.reset();
  if (!gba_shared_alloc()) {
    gba_shared_free();
    return;
  }
  // the two frame buffers video_cb alternates between, before the ROM cache
  // takes what PSRAM is left (allocated on first use, they came after it, and
  // with the second one missing no frame reached the screen)
  auto &emu = BoxEmu::get();
  if (!emu.frame_buffer0() || !emu.frame_buffer1()) {
    fmt::print("gba: no memory for the frame buffers\n");
    gba_shared_free();
    return;
  }
  retro_set_environment(&environ_cb);
  retro_set_video_refresh(&video_cb);
  retro_set_audio_sample_batch(&audio_batch_cb);
  retro_set_input_poll(&input_poll_cb);
  retro_set_input_state(&input_state_cb);
  retro_init();
  check_heap("after retro_init");
  struct retro_game_info info = {};
  info.path = rom_path.c_str();
  if (!retro_load_game(&info)) {
    fmt::print("gba: could not load '{}'\n", rom_path);
    retro_deinit();
    gba_shared_free();
    return;
  }
  struct retro_system_av_info av = {};
  check_heap("after retro_load_game");
  retro_get_system_av_info(&av);
  g_fps = av.timing.fps;
  emu.palette(nullptr);
  g_audio_rate = static_cast<uint32_t>(av.timing.sample_rate);
  emu.audio_sample_rate(g_audio_rate);
  emu.audio_max_wait_ms(30);
  emu.native_size(GBA_W, GBA_H, GBA_W);
  g_frame_index = 0;
  g_last_elapsed = 0;
  g_skip_run = 0;
  g_render_budget.reset();
  reset_frame_time();
  g_rendered_frames = g_rendered_us = g_skipped_frames = g_skipped_us = 0;
  gba_metrics_reset();
  g_next_due = 0;
#if CONFIG_JK_ESP_DEBUG
  if (jk_prof_start) {
    jk_prof_start(); // PC histogram of core 0 (this task), dumped at deinit
  }
#endif
#ifdef TAB5_GBA_RV32
  extern int gba_actual_rv32();
  gba_metrics_backend(gba_actual_rv32() != 0);
#else
  gba_metrics_backend(false);
#endif
  g_initialized = true;
  g_storage_ready =
      g_battery.configure("gba", hash,
                          retro_get_memory_size(RETRO_MEMORY_SAVE_RAM)) &&
      g_battery.load(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
  if (!g_storage_ready)
    fmt::print(
        "gba: no valid battery generation; game refused, files preserved\n");
  check_heap("after battery load");
  fmt::print("gba: '{}' loaded, {:.2f} fps, audio {} Hz, state {} bytes\n",
             rom_path, g_fps, static_cast<unsigned>(av.timing.sample_rate),
             retro_serialize_size());
}

extern "C" {
void __real_execute_arm(uint32_t);
uint32_t __real_update_gba(int);
void __real_update_scanline();
uint8_t *__real_load_gamepak_page(uint32_t);
int64_t __real_filestream_read(RFILE *, void *, int64_t);
int64_t __wrap_filestream_read(RFILE *file, void *data, int64_t len) {
  auto t = esp_timer_get_time();
  auto n = __real_filestream_read(file, data, len);
  g_sample.sd_read_us += esp_timer_get_time() - t;
  if (n > 0)
    g_sample.sd_read_bytes += n;
  ++g_sample.sd_read_calls;
  return n;
}
void __wrap_execute_arm(uint32_t cycles) {
  auto t = esp_timer_get_time();
  __real_execute_arm(cycles);
  g_sample.execute_us += esp_timer_get_time() - t;
}
uint32_t __wrap_update_gba(int cycles) {
  auto t = esp_timer_get_time();
  auto r = __real_update_gba(cycles);
  g_sample.hardware_us += esp_timer_get_time() - t;
  return r;
}
void __wrap_update_scanline() {
  auto t = esp_timer_get_time();
  __real_update_scanline();
  g_sample.ppu_us += esp_timer_get_time() - t;
}
uint8_t *__wrap_load_gamepak_page(uint32_t page) {
  auto t = esp_timer_get_time();
  auto *r = __real_load_gamepak_page(page);
  g_sample.rom_page_us += esp_timer_get_time() - t;
  ++g_sample.rom_page_loads;
  return r;
}
#ifdef TAB5_GBA_RV32
extern int dynarec_enable;
uint32_t __real_execute_arm_translate(uint32_t);
uint32_t __wrap_execute_arm_translate(uint32_t cycles) {
  auto t = esp_timer_get_time();
  auto r = __real_execute_arm_translate(cycles);
  g_sample.execute_us += esp_timer_get_time() - t;
  return r;
}
void tab5_gba_record_jit(uint32_t elapsed) { g_sample.jit_us += elapsed; }

#endif
}
#ifdef TAB5_GBA_RV32
int gba_actual_rv32() { return dynarec_enable; }
#endif

void reset_gba_core() {
  if (g_initialized) {
    retro_reset();
    BoxEmu::get().discard_audio();
  }
}

bool gba_loaded() { return g_initialized && g_storage_ready; }

void run_gba_rom() {
  if (!g_initialized)
    return;
  const auto start = esp_timer_get_time();
  const uint64_t frame_us = static_cast<uint64_t>(1e6 / g_fps);
  // cumulative pacing: frames are due at g_next_due, one frame apart; a
  // fast frame does not sleep the difference away when we are behind, it
  // pays back a slow one. Far behind (> 2 frames) -> resynchronize.
  if (g_next_due == 0 || start > g_next_due + 2 * frame_us) {
    g_next_due = start;
  }
  // the sleep below wakes up to a tick (1 ms) after the deadline, so a frame
  // that starts just past it is on time; only a quarter frame late is behind
  // (with no slack every frame after a sleep was "behind" and every other
  // frame was skipped, even at 4 ms a frame)
  const bool behind = start > g_next_due + frame_us / 4;
  // Bound consecutive visual skips using the measured rendering budget.
  // The emulated CPU, timers and audio still execute every frame.
  if (g_audio_status_cb) {
    const bool skip = !g_render_budget.should_render(behind,frame_us);
    g_audio_status_cb(true, skip ? 0 : 100, skip);
  }
  g_frame_rendered = false;
  g_sample = {};
  retro_run();
  check_heap("after retro_run");
  const auto end = esp_timer_get_time();
  const uint64_t elapsed = end - start;
  g_render_budget.completed(g_frame_rendered,elapsed);
  g_last_elapsed = elapsed;
  update_frame_time(elapsed);
  if (g_frame_rendered) {
    g_rendered_frames++;
    g_rendered_us += elapsed;
  } else {
    g_skipped_frames++;
    g_skipped_us += elapsed;
  }
  g_next_due += frame_us;
  // (no dynamic audio rate control: stretching the audio to the emulation
  // speed fills the DAC queue, the audio call then blocks the emulator and
  // the loop runs away; the occasional gap when a scene is slow is better)
  if (end < g_next_due) {
    std::this_thread::sleep_for(std::chrono::microseconds(g_next_due - end));
  } else {
    std::this_thread::yield();
  }
  g_sample.run_us = elapsed;
  g_sample.drawn = g_frame_rendered;
  g_sample.pacing_us = esp_timer_get_time() - end;
  gba_metrics_record(g_sample);
}

bool load_gba(std::string_view save_path) {
  if (!g_initialized || save_path.empty())
    return false;
  const auto expected = retro_serialize_size();
  tab5::content::FileBytes buf;
  if (!tab5::content::read_exact_file(std::string(save_path), expected, buf))
    return false;
  if (!retro_unserialize(buf.data(), buf.size())) {
    fmt::print("gba: state load failed ({} bytes, expected {})\n", buf.size(),
               retro_serialize_size());
    return false;
  }
  BoxEmu::get().discard_audio();
  check_heap("after retro_unserialize");
  return true;
}

bool save_gba(std::string_view save_path) {
  if (!g_initialized || save_path.empty())
    return false;
  std::vector<uint8_t> buf(retro_serialize_size());
  if (!retro_serialize(buf.data(), buf.size()))
    return false;
  return tab5::content::atomic_write(std::string(save_path), buf.data(),
                                     buf.size());
}

bool flush_gba_battery() {
  if (!g_initialized)
    return true;
  check_heap("before battery flush");
  auto *data = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
  return g_storage_ready && g_battery.flush(data);
}

std::span<uint8_t> get_gba_video_buffer() {
  if (!g_initialized || !g_last_frame)
    return {};
  return {g_last_frame, GBA_W * GBA_H * 2};
}

void deinit_gba() {
  if (!g_initialized)
    return;
  check_heap("before deinit");
  if (g_storage_ready && !flush_gba_battery())
    fmt::print(
        "gba: battery save FAILED; previous disk generation preserved\n");
  g_battery.reset();
  g_storage_ready = false;
  g_initialized = false;
#if CONFIG_JK_ESP_DEBUG
  if (jk_prof_dump) {
    jk_prof_dump(200);
  }
#endif
  fmt::print(
      "gba: {} rendered frames at {:.1f} ms, {} skipped frames at {:.1f} ms\n",
      g_rendered_frames,
      g_rendered_frames ? g_rendered_us / 1000.0 / g_rendered_frames : 0.0,
      g_skipped_frames,
      g_skipped_frames ? g_skipped_us / 1000.0 / g_skipped_frames : 0.0);
  retro_unload_game();
  retro_deinit();
  check_heap("after retro_deinit");
  gba_shared_free();
  BoxEmu::get().audio_sample_rate(48000);
  g_last_frame = nullptr;
  g_audio_status_cb = nullptr;
}
