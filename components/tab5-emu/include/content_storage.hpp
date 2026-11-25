#pragma once
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <string>
#include <array>
#ifdef ESP_PLATFORM
#include "esp_vfs_fat.h"
#else
#include <sys/statvfs.h>
#endif
#include <unistd.h>

namespace tab5::content {
std::string rom_sha256(const std::string &path);
bool digest_bytes(const void *data,size_t size,std::array<uint8_t,32> &digest);
// Keep the previous completed generation. A power loss after the rotation is
// recoverable through readable_generation(). Never truncate the live file.
template <class Writer>
inline bool atomic_file_write(const std::string &path,size_t maximum_size,Writer writer) {
  std::error_code ec;
  const auto dir = std::filesystem::path(path).parent_path();
  std::filesystem::create_directories(dir, ec);
  if (ec) return false;
  uint64_t free_bytes = 0;
#ifdef ESP_PLATFORM
  uint64_t total_bytes = 0;
  if (esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes) != ESP_OK) return false;
#else
  struct statvfs space{};
  if (statvfs(dir.c_str(), &space) != 0) return false;
  free_bytes = static_cast<uint64_t>(space.f_bavail) * space.f_frsize;
#endif
  if (free_bytes < maximum_size || free_bytes - maximum_size < 4096) return false;
  const auto temporary = path + ".tmp", backup = path + ".bak";
  FILE *file = fopen(temporary.c_str(), "wb");
  if (!file) return false;
  bool ok = writer(file) && !ferror(file);
  const long written=ftell(file);
  if (written<0 || static_cast<uint64_t>(written)>maximum_size) ok=false;
  if (fflush(file) != 0) ok = false;
  if (fsync(fileno(file)) != 0) ok = false;
  if (fclose(file) != 0) ok = false;
  if (!ok) { remove(temporary.c_str()); return false; }
  if (std::filesystem::exists(path, ec)) {
    // Removing the old backup cannot damage the current valid generation.
    if (std::filesystem::exists(backup, ec) && remove(backup.c_str()) != 0) return false;
    if (rename(path.c_str(), backup.c_str()) != 0) return false;
  }
  if (rename(temporary.c_str(), path.c_str()) != 0) {
    if (!std::filesystem::exists(path, ec)) rename(backup.c_str(), path.c_str());
    return false;
  }
  return true;
}
bool atomic_write(const std::string &path,const void *data,size_t size);
std::string readable_generation(const std::string &path);
}
