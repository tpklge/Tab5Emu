#pragma once
#if defined(ENABLE_N64_CPU_PROBE)
#include "content_storage.hpp"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "format.hpp"
#include "fixture_identity.h"
#include "probe.h"
#include <fstream>
#include <array>
#include <vector>
inline void run_n64_cpu_probe_from_sd() {
  const auto path =
      tab5::content::resolve("benchmarks/tab5-n64-cpu-probe.mips");
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file)
    return;
  if (file.tellg() != N64_PROBE_BYTES)
    return;
  std::vector<uint8_t> bytes(N64_PROBE_BYTES);
  file.seekg(0);
  file.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
  file.close();
  std::array<uint8_t, 32> digest{};
  if (!tab5::content::digest_bytes(bytes.data(), bytes.size(), digest))
    return;
  auto hex = [](const uint8_t *p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) {
      s += "0123456789abcdef"[p[i] >> 4];
      s += "0123456789abcdef"[p[i] & 15];
    }
    return s;
  };
  if (hex(digest.data(), 32) != N64_PROBE_SHA256)
    return;
  n64_probe_result result{};
  const int64_t start = esp_timer_get_time();
  const bool passed = n64_probe_run(bytes.data(), bytes.size(), &result);
  const auto *app = esp_app_get_description();
  const std::string report = fmt::format(
      "{{\"firmware\":\"{}\",\"elf_sha256\":\"{}\",\"fixture_sha256\":\"{}\","
      "\"passed\":{},\"instructions\":{},\"core_bytes\":{},\"cause\":{},"
      "\"epc\":{},\"elapsed_us\":{},\"unsupported\":\"{}\"}}\n",
      app->version, hex(app->app_elf_sha256, 32), N64_PROBE_SHA256, passed,
      result.instructions, result.core_bytes, result.cause, result.epc,
      esp_timer_get_time() - start,
      result.unsupported ? result.unsupported : "");
  const auto output = tab5::content::resolve(
      std::string("benchmarks/n64-cpu-probe-") + app->version + ".json");
  if (!tab5::content::atomic_write(output, report.data(), report.size()))
    fmt::print("N64 CPU probe report write failed\n");
  fmt::print("TAB5N64_RESULT {}", report);
}
#else
inline void run_n64_cpu_probe_from_sd() {}
#endif
