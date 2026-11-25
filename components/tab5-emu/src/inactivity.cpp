#include "esp_timer.h"
#include "tab5-emu.hpp"
#include <algorithm>

void Tab5Emu::brightness(float value) {
  desired_brightness_ = std::clamp(value, 5.0f, 100.0f);
  if (!screen_sleeping_)
    Bsp::get().brightness(desired_brightness_);
}
void Tab5Emu::input_activity() { last_input_us_ = esp_timer_get_time(); }
bool Tab5Emu::initialize_inactivity() {
  input_activity();
  return xTaskCreate(
             [](void *context) {
               auto &self = *static_cast<Tab5Emu *>(context);
               for (;;) {
                 const auto seconds = self.idle_seconds_.load();
                 const bool asleep =
                     seconds && (!self.game_active_ || self.idle_in_game_) &&
                     (esp_timer_get_time() - self.last_input_us_.load()) >=
                         int64_t(seconds) * 1000000;
                 if (asleep != self.screen_sleeping_.load()) {
                   std::lock_guard<std::recursive_mutex> lk(lvgl_mutex());
                   self.screen_sleeping_ = asleep;
                   if (asleep) {
                     self.wake_block_ = true;
                     self.press_latch_ = 0;
                   }
                   Bsp::get().brightness(
                       asleep ? 0 : self.desired_brightness_.load());
                   for (auto *indev = lv_indev_get_next(nullptr); indev;
                        indev = lv_indev_get_next(indev)) {
                     lv_indev_enable(indev, !asleep);
                     if (!asleep)
                       lv_indev_wait_release(indev);
                   }
                 }
                 if (!self.screen_sleeping_ && self.wake_block_)
                   (void)self.gamepad_state();
                 vTaskDelay(pdMS_TO_TICKS(50));
               }
             },
             "screen-idle", 3072, this, 1, nullptr) == pdPASS;
}
