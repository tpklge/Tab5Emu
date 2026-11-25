#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
bool gb_memory_init();
bool init_gameboy_checked(const std::string &path, uint8_t *rom, size_t bytes);
