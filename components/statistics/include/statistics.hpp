#pragma once

#include <algorithm>
#include <atomic>

#include "format.hpp"

void update_frame_time(uint64_t frame_time);
void reset_frame_time();

float get_fps();
// Actual displayed images per second, frozen while the game is paused.
float get_presented_fps();
void reset_presented_frame_rate();
void resume_presented_frame_rate();
uint64_t get_frame_time();
uint64_t get_frame_time_max();
uint64_t get_frame_time_min();
float get_frame_time_avg();

void print_statistics();
