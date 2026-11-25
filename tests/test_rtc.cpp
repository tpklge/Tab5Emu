#include "gb_rtc_math.h"
#include "gb_pause_clock.hpp"
extern "C" {
#include "gnuboy/rtc.h"
}
#include <cassert>
#include <cstdint>
#include <iostream>
int main() {
  {
    tab5::gb::PauseClock clock;
    assert(clock.poll(1000000, false) == 0);
    clock.begin(100); clock.begin(999999); // nested pre-menu is idempotent
    assert(clock.poll(1000100, false) == 60);
    assert(clock.poll(1000200, false) == 0);
    assert(clock.end(2000100, false) == 60); // flush + resume never double-count
    assert(!clock.active() && clock.end(9000000, false) == 0);
    clock.begin(10000000);
    assert(clock.poll(12000000, true) == 0); // MBC3 halt discards paused time
    assert(clock.end(13000000, false) == 60);
    clock.begin(14000000);
    assert(clock.poll(13000000, false) == 0); // no underflow
    assert(clock.end(14000000, false) == 60);
    clock = {}; clock.begin(0);
    uint64_t total = 0;
    for (unsigned n=1; n<=10000; ++n) total += clock.poll(n*100, false);
    assert(total == 60); // fractional polls retain a full second
  }
  rtc = {};
  rtc.d = 511;
  rtc.h = 23;
  rtc.m = 59;
  rtc.s = 59;
  rtc.t = 59;
  rtc_tick();
  assert(rtc.d == 0 && rtc.h == 0 && rtc.m == 0 && rtc.s == 0 &&
         rtc.carry == 1);
  rtc = {};
  rtc.d = 365;
  tab5_gb_rtc_advance(&rtc.d, &rtc.h, &rtc.m, &rtc.s, 0, &rtc.carry, 86400);
  assert(rtc.d == 366 && !rtc.carry);
  rtc.sel = 12;
  rtc_write(1);
  assert(rtc.d == 366);
  rtc_latch(0);
  rtc_latch(1);
  assert(rtc.regs[4] == 1);
  rtc.sel = 11;
  rtc_write(0);
  assert(rtc.d == 256);
  rtc.sel = 12;
  rtc_write(0x40);
  rtc.t = 42;
  rtc_tick();
  assert(rtc.t == 42);
  tab5_gb_rtc_advance(&rtc.d, &rtc.h, &rtc.m, &rtc.s, rtc.stop, &rtc.carry,
                      UINT64_MAX);
  assert(rtc.d == 0 && rtc.t == 42);
  rtc.stop = 0;
  tab5_gb_rtc_advance(&rtc.d, &rtc.h, &rtc.m, &rtc.s, 0, &rtc.carry,
                      512ULL * 86400 * 4 + 90061);
  assert(rtc.d == 1 && rtc.h == 1 && rtc.m == 1 && rtc.s == 1 &&
         rtc.carry == 1);
  std::cout << "RTC: actual core day-high/latch/512-day carry, halt and "
               "constant-time offline arithmetic OK\n";
}
