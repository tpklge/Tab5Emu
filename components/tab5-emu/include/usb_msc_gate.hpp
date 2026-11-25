#pragma once
#include <cstdint>
#include <atomic>
#include <mutex>
namespace tab5::usb {
enum class HostIo { Read, Write, Ready };
struct MscIoSnapshot {
  bool allowed{false};
  uint32_t blocked_reads{0}, blocked_writes{0}, blocked_ready{0};
};
// Closing waits for any accepted callback to return (including enqueueing a
// deferred write). The USB driver's subsequent barriers then drain those
// writes before mounting FAT. New callbacks cannot reach the medium.
class MscIoGate {
public:
  void allow(bool on) {
    std::lock_guard<std::mutex> lock(mutex_);
    allowed_=on;
  }
  template<class F> int32_t run(HostIo kind,F operation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(!allowed_){
      if(kind==HostIo::Read)++reads_;
      else if(kind==HostIo::Write)++writes_;
      else ++ready_;
      return -1;
    }
    return operation();
  }
  MscIoSnapshot snapshot() const {
    return {allowed_.load(),reads_.load(),writes_.load(),ready_.load()};
  }
private:
  mutable std::mutex mutex_;
  std::atomic<bool> allowed_{false};
  std::atomic<uint32_t> reads_{0},writes_{0},ready_{0};
};
void allow_host_io(bool on);
MscIoSnapshot msc_io_snapshot();
} // namespace tab5::usb
