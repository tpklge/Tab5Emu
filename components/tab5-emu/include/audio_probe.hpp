#pragma once
#include <cstdint>
namespace tab5::audio_probe {
struct Snapshot {uint64_t blocks,partial_blocks,empty_blocks,missing_frames,max_write_gap_us,write_errors,late_blocks,late_frames;
uint64_t startup_blocks,startup_partial_blocks,startup_empty_blocks,startup_missing_frames,startup_write_errors;};
void reset();void expected(bool active);Snapshot snapshot();
}
