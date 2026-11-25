#include "touch_controls.hpp"
#include "esp_cache.h"
#include "tab5-emu.hpp"
#include <cstring>

void Tab5Emu::touch_controls(bool enabled) {
  touch_controls_enabled_ = enabled;
  touch_controls_dirty_ = true;
  std::lock_guard<std::mutex> lk(touch_gamepad_.mutex);
  touch_gamepad_.state = {};
  press_latch_ = 0;
}
std::vector<uint8_t> Tab5Emu::capture_lcd() {
  std::lock_guard<std::recursive_mutex> lk(video_frame_mutex_);
  auto *fb = static_cast<uint16_t *>(dpi_frame_buffer());
  if (!fb)
    return {};
  // Called between emulated frames. The video worker holds this mutex too.
  esp_cache_msync(fb, dpi_fb_bytes_, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
  std::vector<uint8_t> copy(lcd_width() * lcd_height() * 2);
  auto *out = reinterpret_cast<uint16_t *>(copy.data());
  for (int y = 0; y < 720; ++y)
    for (int x = 0; x < 1280; ++x)
      out[y * 1280 + x] = fb[(1279 - x) * 720 + y];
  return copy;
}
void Tab5Emu::draw_touch_controls() {
  if (!touch_controls())
    return;
  const auto state = gamepad_state();
  if (!touch_controls_dirty_.exchange(false) &&
      state.buttons == controls_last_buttons_)
    return;
  controls_last_buttons_ = state.buttons;
  auto *fb = static_cast<uint16_t *>(dpi_frame_buffer());
  if (!fb)
    return;
  auto pixel = [&](int x, int y, uint16_t c) {
    if (x >= 0 && x < 1280 && y >= 0 && y < 720)
      fb[(1279 - x) * 720 + y] = c;
  };
  auto rect = [&](int x, int y, int w, int h, uint16_t c) {
    for (int xx = x; xx < x + w; ++xx)
      for (int yy = y; yy < y + h; ++yy)
        pixel(xx, yy, c);
  };
  rect(0, 0, 208, 720, 0x0843);
  rect(1072, 0, 208, 720, 0x0843);
  // Minimal original 5x7 glyphs for control labels, scaled 3x.
  auto glyph = [](char c) -> const char * {
    switch (c) {
    case 'A':
      return "01110100011000111111100011000110001";
    case 'B':
      return "11110100011000111110100011000111110";
    case 'X':
      return "10001100010101000100010101000110001";
    case 'Y':
      return "10001100010101000100001000010000100";
    case 'Z':
      return "11111000010001000100010001000011111";
    case 'O':
      return "01110100011000110001100011000101110";
    case 'D':
      return "11110100011000110001100011000111110";
    case 'L':
      return "10000100001000010000100001000011111";
    case 'R':
      return "11110100011000111110101001001010001";
    case 'S':
      return "01111100001000001110000010000111110";
    case 'E':
      return "11111100001000011110100001000011111";
    case 'C':
      return "01111100001000010000100001000001111";
    case 'T':
      return "11111001000010000100001000010000100";
    case 'M':
      return "10001110111010110101100011000110001";
    case 'N':
      return "10001110011010110011100011000110001";
    case 'U':
      return "10001100011000110001100011000101110";
    default:
      return "00000000000000000000000000000000000";
    }
  };
  auto text = [&](int cx, int cy, const char *s) {
    int x = cx - int(strlen(s) * 18 - 3) / 2;
    for (; *s; ++s, x += 18) {
      const char *g = glyph(*s);
      for (int y = 0; y < 7; ++y)
        for (int z = 0; z < 5; ++z)
          if (g[y * 5 + z] == '1')
            rect(x + z * 3, cy - 10 + y * 3, 3, 3, 0xffff);
    }
  };
  for (const auto &b : tab5::touch::buttons_for(touch_profile_.load()))
    if (tab5::touch::active(b, touch_profile_.load())) {
      rect(b.x, b.y, b.w, b.h, 0x424d);
      rect(b.x + 2, b.y + 2, b.w - 4, b.h - 4,
           (state.buttons & b.mask) ? 0x731b : 0x18a6);
      const char *label = b.label;
      if (touch_profile_ == tab5::input::Profile::MegaDrive || touch_profile_ == tab5::input::Profile::MegaDrive6) {
        if (b.mask == 1)
          label = "B";
        else if (b.mask == 2)
          label = "A";
        else if (b.mask == 8)
          label = "C";
        else if(b.mask==1024)label="Y";
        else if(b.mask==2048)label="Z";
        else if(b.mask==16)label="MODE";
      }
      text(b.x + b.w / 2, b.y + b.h / 2, label);
    }
  // Four visible arrow heads use the same center and diagonal region as hit().
  for (int direction = 0; direction < 4; ++direction) {
    const uint16_t mask = uint16_t(64 << direction);
    const bool up = direction == 0, down = direction == 1,
               left = direction == 2;
    const int cx = left             ? 48
                   : direction == 3 ? 160
                                    : 104,
              cy = up     ? 314
                   : down ? 426
                          : 370;
    rect(cx - 26, cy - 26, 52, 52, (state.buttons & mask) ? 0x731b : 0x424d);
    for (int a = -14; a <= 14; ++a)
      for (int b = 0; b < 15 - std::abs(a); ++b) {
        if (up)
          pixel(cx + a, cy + 7 - b, 0xffff);
        else if (down)
          pixel(cx + a, cy - 7 + b, 0xffff);
        else if (left)
          pixel(cx + 7 - b, cy + a, 0xffff);
        else
          pixel(cx - 7 + b, cy + a, 0xffff);
      }
  }
  // Portrait panel rows correspond to logical columns: sync the two gutters.
  constexpr size_t gutter_bytes = 208 * 720 * 2;
  esp_cache_msync(fb, gutter_bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
  esp_cache_msync(fb + (1280 - 208) * 720, gutter_bytes,
                  ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}
