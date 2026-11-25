#pragma once
#include <cstdint>

namespace tab5::video {
// Skip pixels only when late. More consecutive skips help only when a frame
// without rendering actually fits the emulated frame's real-time budget.
class RenderBudget {
public:
  bool should_render(bool behind,uint64_t period_us) const {
    const unsigned limit=skipped_us_ && skipped_us_<period_us ? 3:1;
    return !behind || rendered_us_<=period_us || consecutive_>=limit;
  }
  void completed(bool rendered,uint64_t elapsed_us) {
    if(rendered){rendered_us_=elapsed_us;consecutive_=0;}
    else {skipped_us_=elapsed_us;if(consecutive_<3)++consecutive_;}
  }
  void reset(){*this={};}
private:
  uint64_t rendered_us_{0},skipped_us_{0};
  unsigned consecutive_{0};
};
}
