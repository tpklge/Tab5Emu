#include "paludo_platform.hpp"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include <cstring>
#include <atomic>

namespace { std::atomic<bool> return_requested{false}; }
void paludo_request_return() { return_requested=true; }
bool paludo_return_requested() { return return_requested.load(); }

bool paludo_launcher_available() {
  const auto *factory = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "launcher");
  const auto *request = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "launch_req");
  esp_app_desc_t descriptor{};
  uint32_t capability = 0;
  // Relative capability record from Paludo's boot_request.h, not a guessed
  // flash address. No request is written: the bootloader consumes launches.
  return factory && request && request->size >= 68 &&
         esp_ota_get_partition_description(factory, &descriptor) == ESP_OK &&
         std::strcmp(descriptor.project_name, "tab5_launcher") == 0 &&
         esp_partition_read(request, 64, &capability, sizeof(capability)) == ESP_OK &&
         capability == 0x3150434c;
}
bool paludo_return_to_launcher() {
  if (!paludo_launcher_available()) return false;
  esp_restart();
  return true;
}
