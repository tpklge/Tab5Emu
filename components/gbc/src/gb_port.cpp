// Derived from esp-cpp/gnuboy port/gameboy.cpp at 4585d0a (GPLv2).
// Local adapter: exact mixer/DAC rate, native timing, no hidden turbo.
// #pragma GCC optimize ("Ofast")

#include "gameboy.hpp"
#include "gbc_shared_memory.hpp"
#include "shared_memory.h"
#include "gb_memory.hpp"
#include "gb_rom.hpp"

#include <memory>

#include "box-emu.hpp"
#include "statistics.hpp"
#include "esp_rom_sys.h"

static const size_t GAMEBOY_SCREEN_WIDTH = 160;
static const size_t GAMEBOY_SCREEN_HEIGHT = 144;
// GAMEBOY_AUDIO_SAMPLE_RATE / GBC_AUDIO_BUFFER_SIZE come from gbc_shared_memory.hpp

extern "C" {
#include <gnuboy/loader.h>
#include <gnuboy/hw.h>
#include <gnuboy/lcd.h>
#include <gnuboy/fb.h>
#include <gnuboy/cpu.h>
#include <gnuboy/mem.h>
#include <gnuboy/sound.h>
#include <gnuboy/pcm.h>
#include <gnuboy/regs.h>
#include <gnuboy/rtc.h>
#include <gnuboy/gnuboy.h>
void tab5_gb_audio_filter_reset(void);
}

using namespace std::chrono_literals;

// need to have these here for gnuboy to work
uint32_t frame = 0;
uint16_t* displayBuffer[2];

static uint16_t* framebuffer;
struct fb fb;
struct pcm pcm;
static uint8_t currentBuffer = 0;
static bool gameboy_initialized = false;
static uint64_t emulated_dsc=0,frame_dsc=0,rtc_phase=0;
static void advance_cpu(int clocks) {const int ran=cpu_emulate(clocks);if(ran>0){emulated_dsc+=ran;frame_dsc+=ran;}}

// cppcheck-suppress constParameterPointer
extern "C" void die(char *fmt, ...) {
  // do nothing...
}

void run_to_vblank() {
  /* FRAME BEGIN */
  /* FIXME: judging by the time specified this was intended
  to emulate through vblank phase which is handled at the
  end of the loop. */
  advance_cpu(2280);
  // cpu_emulate(2280 / 2);

  /* FIXME: R_LY >= 0; comparsion to zero can also be removed
  altogether, R_LY is always 0 at this point */
  while (R_LY > 0 && R_LY < GAMEBOY_SCREEN_HEIGHT)
  {
    advance_cpu(cpu->lcdc);
  }

  /* VBLANK BEGIN */
  if ((frame % 2) == 0) {
    BoxEmu::get().push_frame(framebuffer);

    // swap buffers
    currentBuffer = currentBuffer ? 0 : 1;
    framebuffer = displayBuffer[currentBuffer];
    fb.ptr = reinterpret_cast<uint8_t*>(framebuffer);
  }



  sound_mix();

  if (pcm.pos > 100) {
    auto audio_sample_count = pcm.pos;
    auto audio_buffer = (uint8_t*)pcm.buf;
    BoxEmu::get().play_audio(audio_buffer, audio_sample_count * sizeof(int16_t));
    pcm.pos = 0;
  }

  if (!(R_LCDC & 0x80)) {
    /* LCDC operation stopped */
    /* FIXME: judging by the time specified, this is
    intended to emulate through visible line scanning
    phase, even though we are already at vblank here */
    // cpu_emulate(32832); // TODO: this was the original, but WHY?
    advance_cpu(32832);
    // cpu_emulate(32832 / 4);
  }

  while (R_LY > 0) {
    /* Step through vblank phase */
    advance_cpu(cpu->lcdc);
  }
  ++frame;
}

void reset_gameboy() {
  if (!gameboy_initialized) return;
  emu_reset();
}

static int64_t frame_deadline_us=0;
static uint32_t frame_fraction=0;

void init_gameboy(const std::string& rom_filename, uint8_t *romdata, size_t rom_data_size) {
  (void)init_gameboy_checked(rom_filename, romdata, rom_data_size);
}
bool init_gameboy_checked(const std::string& rom_filename, uint8_t *romdata, size_t rom_data_size) {
  if (gameboy_initialized || tab5::gb::rom_error({romdata, rom_data_size})) return false;
  // Initialize shared memory
  if (!gb_memory_init()) return false;

  // Get shared memory regions
  uint8_t* vram = nullptr;
  uint8_t* wram = nullptr;
  uint8_t* audio = nullptr;
  gbc_get_memory_regions(&vram, &wram, &audio);

  // Use shared memory regions
  lcd->vbank = vram;
  ram.ibank = wram;
  pcm.buf = reinterpret_cast<int16_t*>(audio);
  // GBC_AUDIO_BUFFER_SIZE (bytes) is the size actually allocated for pcm.buf in
  // gbc_shared_memory.cpp, so pcm.len matches the buffer exactly.
  pcm.len = (GBC_AUDIO_BUFFER_SIZE / (2*sizeof(int16_t))) * 2;

  // set native size
  BoxEmu::get().native_size(GAMEBOY_SCREEN_WIDTH, GAMEBOY_SCREEN_HEIGHT);
  BoxEmu::get().palette(nullptr);

  displayBuffer[0] = (uint16_t*)BoxEmu::get().frame_buffer0();
  displayBuffer[1] = (uint16_t*)BoxEmu::get().frame_buffer1();
  if (!displayBuffer[0] || !displayBuffer[1]) {
    gbc_free_shared_memory();
    framebuffer = nullptr;
    return false;
  }

  memset(&fb, 0, sizeof(fb));
  fb.w = GAMEBOY_SCREEN_WIDTH;
  fb.h = GAMEBOY_SCREEN_HEIGHT;
  fb.pelsize = 2;
  fb.pitch = fb.w * fb.pelsize;
  fb.indexed = 0;
  fb.ptr = reinterpret_cast<uint8_t*>(displayBuffer[0]);
  fb.enabled = 1;
  fb.dirty = 0;
  framebuffer = displayBuffer[0];

  BoxEmu::get().audio_sample_rate(GAMEBOY_AUDIO_SAMPLE_RATE);
  // save the audio buffer
  auto buf = pcm.buf;
  auto len = pcm.len;
  memset(&pcm, 0, sizeof(pcm));
  pcm.hz = GAMEBOY_AUDIO_SAMPLE_RATE;
  pcm.stereo = 1;
  pcm.len = len;
  pcm.buf = buf;
  pcm.pos = 0;

  frame_deadline_us=0;frame_fraction=0;emulated_dsc=frame_dsc=rtc_phase=0;
  BoxEmu::get().clear_audio();

  sound_reset();

  memset(&rtc,0,sizeof(rtc)); // A new cartridge must not inherit the prior RTC selector/halt.
  // loader_init's legacy void API discards rom_load's failure result.
  if (rom_load(romdata, rom_data_size) != 0) {
    loader_unload();
    gbc_free_shared_memory();
    rom.bank = nullptr; rom.length = 0;
    framebuffer = nullptr; fb.ptr = nullptr;
    return false;
  }
  emu_reset();
  frame = 0;
  reset_frame_time();
  currentBuffer = 0;
  gameboy_initialized = true;
  return true;
}

void run_gameboy_rom() {
  if (!gameboy_initialized) return;
  auto start = esp_timer_get_time();
  // GET INPUT
  auto state = BoxEmu::get().gamepad_state();
  pad_set(PAD_UP, state.up);
  pad_set(PAD_DOWN, state.down);
  pad_set(PAD_LEFT, state.left);
  pad_set(PAD_RIGHT, state.right);
  pad_set(PAD_SELECT, state.select);
  pad_set(PAD_START, state.start);
  pad_set(PAD_A, state.a);
  pad_set(PAD_B, state.b);
  frame_dsc=0;
  run_to_vblank();
  rtc_phase+=frame_dsc*60;
  while(rtc_phase>=2097152){rtc_phase-=2097152;rtc_tick();}
  BoxEmu::get().audio_clock(emulated_dsc,2097152);
  auto end = esp_timer_get_time();

  update_frame_time(end-start);
  // Pace the CPU clocks actually executed (gnuboy uses 2 MHz units).
  // Loop/frame counters are not a clock, especially while the LCD is off.
  if (!frame_deadline_us || end-frame_deadline_us>70'000) frame_deadline_us=start;
  const uint64_t numerator=frame_dsc*1000000;
  frame_deadline_us+=numerator/2097152;
  frame_fraction+=numerator%2097152;
  if (frame_fraction>=2097152) {++frame_deadline_us;frame_fraction-=2097152;}
  int64_t remaining=frame_deadline_us-esp_timer_get_time();
  if (remaining>2000) vTaskDelay(pdMS_TO_TICKS((remaining-1000)/1000));
  remaining=frame_deadline_us-esp_timer_get_time();
  if (remaining>0) esp_rom_delay_us(static_cast<uint32_t>(remaining));

}

void load_gameboy(std::string_view save_path) {
  if (!gameboy_initialized) return;
  if (save_path.size()) {
    auto f = fopen(save_path.data(), "rb");
    if (!f) return;
    loadstate(f);
    tab5_gb_audio_filter_reset();
    fclose(f);
    vram_dirty();
    pal_dirty();
    sound_dirty();
    mem_updatemap();
  }
}

void save_gameboy(std::string_view save_path) {
  if (!gameboy_initialized) return;
  // save state
  auto f = fopen(save_path.data(), "wb");
  if (!f) return;
  savestate(f);
  fclose(f);
}

std::span<uint8_t> get_gameboy_video_buffer() {
  if (!gameboy_initialized || !framebuffer) return {};
  return std::span<uint8_t>(reinterpret_cast<uint8_t*>(framebuffer), GAMEBOY_SCREEN_WIDTH * GAMEBOY_SCREEN_HEIGHT * 2);
}

void deinit_gameboy() {
  if (!gameboy_initialized) return;
  gameboy_initialized = false;
  loader_unload();
  // Free shared memory
  gbc_free_shared_memory();

  // now unload everything
  rom.bank = nullptr; rom.length = 0;
  framebuffer = nullptr; fb.ptr = nullptr;
  displayBuffer[0] = displayBuffer[1] = nullptr;
  BoxEmu::get().audio_sample_rate(48000);
}
