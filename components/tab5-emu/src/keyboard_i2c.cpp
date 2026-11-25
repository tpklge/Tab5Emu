// A164 Normal protocol: bounded polling in its own task; no I2C in ISR/UI.
#include "tab5-emu.hpp"
#include "keyboard_mapping.hpp"
#include "driver/i2c_master.h"
#include "freertos/task.h"

bool Tab5Emu::initialize_i2c_keyboard() {
  i2c_master_bus_handle_t bus{};
  // The BSP owns its internal HP bus. Ext.Port1 GPIO0/1 uses LP I2C.
  // Refuse an occupied bus rather than reconfigure another peripheral's bus.
  if (i2c_master_get_bus_handle(LP_I2C_NUM_0, &bus) == ESP_OK) {
    logger_.warn("Ext.Port1 LP I2C already owned; A164 disabled");
    return false;
  }
  i2c_master_bus_config_t config{};
  config.i2c_port = LP_I2C_NUM_0;
  config.sda_io_num = GPIO_NUM_0;
  config.scl_io_num = GPIO_NUM_1;
  config.lp_source_clk = LP_I2C_SCLK_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;
  if (i2c_new_master_bus(&config, &bus) != ESP_OK) return false;
  i2c_device_config_t device_config{};
  device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  device_config.device_address = 0x6d;
  device_config.scl_speed_hz = 100000;
  i2c_master_dev_handle_t device{};
  if (i2c_master_bus_add_device(bus, &device_config, &device) != ESP_OK) {
    i2c_del_master_bus(bus);
    return false;
  }
  keyboard_device_ = device;
  if (xTaskCreate([](void *ctx) { static_cast<Tab5Emu *>(ctx)->i2c_keyboard_task(); },
                  "a164-input", 4096, this, 5, nullptr) != pdPASS) {
    keyboard_device_ = nullptr;
    i2c_master_bus_rm_device(device);
    i2c_del_master_bus(bus);
    return false;
  }
  return true;
}

void Tab5Emu::i2c_keyboard_task() {
  auto device = static_cast<i2c_master_dev_handle_t>(keyboard_device_);
  auto read = [&](uint8_t reg, uint8_t &v) {
    return i2c_master_transmit_receive(device, &reg, 1, &v, 1, 25) == ESP_OK;
  };
  auto write = [&](uint8_t reg, uint8_t v) {
    uint8_t data[] = {reg, v};
    return i2c_master_transmit(device, data, 2, 25) == ESP_OK;
  };
  tab5::input::Matrix matrix;
  bool connected = false;
  for (;;) {
    if (!connected) {
      uint8_t address = 0;
      connected = read(0xff, address) && address == 0x6d && write(0x10, 0) &&
                  write(0x00, 0) && write(0x02, 0); // Normal, polling, clear stale FIFO
      matrix.held.fill(false);
    }
    if (connected) {
      uint8_t count = 0;
      connected = read(0x02, count);
      // Firmware FIFO holds 32 events. A full FIFO may have dropped releases:
      // clear state and reconfigure, forcing the user to release/repress.
      if (count >= 32) connected = false;
      for (unsigned i = 0; connected && i < count && i < 32; ++i) {
        uint8_t raw = 255;
        connected = read(0x20, raw);
        if (!connected || raw == 255) break;
        if (!matrix.event(raw)) continue;
        KeyboardState report{};
        matrix.bitmap(report.keys);
        // Publish each event, so press+release in one FIFO batch still
        // latches both game actions and the frontend menu correctly.
        std::lock_guard<std::mutex> lock(hid_mutex_);
        publish_keyboard_locked(true,report);
      }
    }
    {
      std::lock_guard<std::mutex> lock(hid_mutex_);
      if (!connected) matrix.held.fill(false);
      KeyboardState report{};
      matrix.bitmap(report.keys);
      publish_keyboard_locked(true,report);
      if (!connected) press_latch_.store(0);
      i2c_keyboard_present_ = connected;
    }
    vTaskDelay(pdMS_TO_TICKS(connected ? 8 : 1000));
  }
}
