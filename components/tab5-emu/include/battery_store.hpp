#pragma once
#include "content_storage.hpp"
#include <string_view>

namespace tab5::content {
// Raw cartridge RAM plus a bounded SHA-256 journal. The journal is committed
// before the data and retains hashes of both live generations and the pending
// write. Legacy raw saves acquire integrity metadata on their first flush.
// Caller freezes emulation for load/flush.
// This object owns no core memory and must be reset before an overlay unload.
class BatteryStore {
public:
  BatteryStore();
  ~BatteryStore();
  bool configure(std::string_view system,std::string_view sha256,size_t bytes,std::string_view filename="battery.sav");
  bool load(void *data);
  bool flush(const void *data);
  void reset();
  const std::string &path() const { return path_; }
private:
  std::string path_;
  size_t size_{0};
  std::array<uint8_t,32> digest_{};
  std::array<std::array<uint8_t,32>,3> accepted_{};
  size_t accepted_count_{0};
  bool ready_{false},digest_valid_{false},from_backup_{false};
  bool journal_seen_{false},journal_repair_{false};
};
// The frontend runs one cart at a time. Keep store objects resident so loading
// overlays repeatedly does not register another global destructor each time.
BatteryStore &primary_battery_store();
BatteryStore &rtc_battery_store();
}
