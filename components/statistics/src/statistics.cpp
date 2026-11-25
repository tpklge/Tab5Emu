#include "statistics.hpp"
#include "presentation_rate.hpp"
#include "esp_timer.h"

static tab5::PresentationRate presentation_rate;
static std::atomic<float> presented_fps{0};
static std::atomic<bool> presentation_reset{true};

float get_presented_fps() { return presented_fps.load(); }
void reset_presented_frame_rate() {
    presented_fps.store(0);
    presentation_reset.store(true);
}
void resume_presented_frame_rate() { presentation_reset.store(true); }

static uint32_t num_frames = 0;
static uint64_t frame_time = 0.0f;
static uint64_t frame_time_total = 0.0f;
static uint64_t frame_time_max = 0.0f;
static uint64_t frame_time_min = 0.0f;
static float frame_time_avg = 0.0f;

void update_frame_time(uint64_t frame_time)
{
    if (presentation_reset.exchange(false)) presentation_rate.reset(false);
    if (presentation_rate.present(esp_timer_get_time())) presented_fps.store(presentation_rate.fps());
    num_frames++;
    ::frame_time = frame_time;
    frame_time_total = frame_time_total + frame_time;
    frame_time_max = std::max(frame_time_max, frame_time);
    frame_time_min = std::min(frame_time_min, frame_time);
    frame_time_avg = float(frame_time_total) / num_frames;
}

void reset_frame_time()
{
    num_frames = 0;
    frame_time = 0;
    frame_time_total = 0;
    frame_time_max = 0;
    frame_time_min = 1000000; // some large number
    frame_time_avg = 0.0f;
}

float get_fps() {
    if (frame_time_total == 0) {
        return 0.0f;
    }
    return num_frames / (frame_time_total / 1e6f);
}

uint64_t get_frame_time() {
    return frame_time;
}

uint64_t get_frame_time_max() {
    return frame_time_max;
}

uint64_t get_frame_time_min() {
    return frame_time_min;
}

float get_frame_time_avg() {
    return frame_time_avg;
}

void print_statistics() {
    fmt::print("Statistics:\n");
    fmt::print("-----------\n");
    fmt::print("Frames: {}\n", num_frames);
    fmt::print("Presented FPS: {:.1f}\n", get_presented_fps());
    fmt::print("Blit capacity: {:.1f} FPS\n", get_fps());
    fmt::print("Frame Time: [min {} us, avg: {:.1f} us, max: {} us]\n", get_frame_time_min(), get_frame_time_avg(), get_frame_time_max());
}
