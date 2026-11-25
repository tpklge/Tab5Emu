#pragma once
#include <string_view>
#include <cstdint>
bool initialize_gameboy_storage(std::string_view system,std::string_view rom_sha256);
bool gameboy_storage_ready();
bool flush_gameboy_battery();
void release_gameboy_storage();
bool save_gameboy_checked(std::string_view path);
bool load_gameboy_checked(std::string_view path);
uint64_t gameboy_frames();
void run_gameboy_tracked();
void pause_gameboy_clock();
void resume_gameboy_clock();
