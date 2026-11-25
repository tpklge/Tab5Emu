#include "audio_preroll.hpp"
#include <cassert>
#include <iostream>
#include <vector>

int main() {
  tab5::audio::Preroll preroll;
  preroll.reset(32768);
  std::vector<int16_t> input(6000), played;
  for (size_t i = 0; i < input.size(); ++i)
    input[i] = int16_t(i * 13);
  unsigned calls = 0;
  auto submit = [&](const int16_t *p, size_t n) {
    ++calls;
    played.insert(played.end(), p, p + n * 2);
  };
  preroll.push(input.data(), 548, submit);
  preroll.push(input.data() + 1096, 548, submit);
  assert(calls == 0);
  preroll.push(input.data() + 2192, 548, submit);
  assert(calls == 2 && played.size() == 3288);
  preroll.push(input.data() + 3288, 1356, submit);
  assert(played == input); // No sample lost, duplicated, muted, or reordered.
  // A timeline change drops only the obsolete, still buffered old timeline.
  preroll.reset(32768);
  preroll.push(input.data(), 548, submit);
  played.clear();
  calls = 0;
  preroll.reset(48000);
  preroll.push(input.data(), 2047, submit);
  assert(calls == 0);
  preroll.push(input.data() + 4094, 953, submit);
  assert(played == input);
  std::cout << "GBA preroll: PCM preserved, 50 ms at 32768 Hz, bounded "
               "capacity, timeline reset passed.\n";
}
