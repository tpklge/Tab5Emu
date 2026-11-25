#include "wall_clock.hpp"
#include "m5stack-tab5.hpp"
#include <array>
#include <ctime>
#include <memory>
#include <mutex>
namespace tab5::content {
namespace {
using Device = espp::I2c::Device<uint8_t>;
Device *clock_device() {
  static std::mutex init;
  std::lock_guard<std::mutex> lock(init);
  static std::shared_ptr<Device> device;
  if (!device) {
    auto &bus = espp::M5StackTab5::get().internal_i2c();
    std::error_code ec;
    device = bus.add_device<uint8_t>(
        {.device_address = espp::M5StackTab5::Rtc::DEFAULT_ADDRESS,
         .timeout_ms = 100,
         .scl_speed_hz = bus.config().clk_speed,
         .log_level = espp::Logger::Verbosity::WARN},
        ec);
    if (ec)
      return nullptr;
  }
  return device.get();
}
std::mutex access;
uint64_t read_clock(Device *device) {
  std::error_code ec;
  uint8_t flags = 0, control = 0;
  if (!device->read_register(0x1d, &flags, 1, ec) || ec || (flags & 2))
    return 0;
  if (!device->read_register(0x1e, &control, 1, ec) || ec || (control & 64))
    return 0;
  std::array<uint8_t, 7> data{};
  bool coherent = false;
  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    uint8_t second = 0;
    if (!device->read_register(0x10, data.data(), data.size(), ec) || ec ||
        !device->read_register(0x10, &second, 1, ec) || ec)
      return 0;
    if ((second & 127) == (data[0] & 127)) {
      coherent = true;
      break;
    }
  }
  if (!coherent)
    return 0;
  auto decimal = [](uint8_t v) -> int {
    if ((v & 15) > 9 || (v >> 4) > 9)
      return -1;
    return (v >> 4) * 10 + (v & 15);
  };
  const int second = decimal(data[0] & 127), minute = decimal(data[1] & 127),
            hour = decimal(data[2] & 63);
  const int day = decimal(data[4] & 63), month = decimal(data[5] & 31),
            year = decimal(data[6]);
  if (second < 0 || second > 59 || minute < 0 || minute > 59 || hour < 0 ||
      hour > 23 || month < 1 || month > 12 || year < 20 || year > 99)
    return 0;
  constexpr int lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (day < 1 || day > lengths[month - 1] + (month == 2 && year % 4 == 0))
    return 0;
  int y = 2000 + year;
  y -= month <= 2;
  const int era = y / 400;
  const unsigned yoe = y - era * 400;
  const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
  const int64_t days =
      era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
  return uint64_t(days) * 86400 + hour * 3600 + minute * 60 + second;
}
} // namespace
uint64_t wall_clock_seconds() {
  auto *device = clock_device();
  if (!device)
    return 0;
  std::lock_guard<std::mutex> lock(access);
  return read_clock(device);
}
bool set_wall_clock_seconds(uint64_t epoch) {
  if (epoch < 1577836800ULL || epoch > 4102444799ULL)
    return false;
  auto *device = clock_device();
  if (!device)
    return false;
  const time_t timestamp = epoch;
  std::tm utc{};
  if (!gmtime_r(&timestamp, &utc))
    return false;
  auto bcd = [](unsigned n) -> uint8_t { return ((n / 10) << 4) | (n % 10); };
  // RX8130CE WEEK is one-hot, not a BCD weekday. No alarm/timer/reset writes.
  const uint8_t data[] = {bcd(utc.tm_sec),
                          bcd(utc.tm_min),
                          bcd(utc.tm_hour),
                          uint8_t(1u << utc.tm_wday),
                          bcd(utc.tm_mday),
                          bcd(utc.tm_mon + 1),
                          bcd((utc.tm_year + 1900) % 100)};
  std::lock_guard<std::mutex> lock(access);
  std::error_code ec;
  uint8_t control = 0, flags = 0;
  if (!device->read_register(0x1e, &control, 1, ec) || ec)
    return false;
  if (!device->write_register(0x10, data, sizeof(data), ec) || ec)
    return false;
  const uint8_t running = control & ~uint8_t(64);
  if (!device->write_register(0x1e, &running, 1, ec) || ec)
    return false;
  if (!device->read_register(0x1d, &flags, 1, ec) || ec)
    return false;
  const uint8_t trusted = flags & ~uint8_t(2);
  if (!device->write_register(0x1d, &trusted, 1, ec) || ec)
    return false;
  const auto actual = read_clock(device);
  return actual >= epoch && actual <= epoch + 2;
}
} // namespace tab5::content
