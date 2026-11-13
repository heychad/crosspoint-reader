#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using esp_ota_handle_t = uint32_t;
inline constexpr esp_err_t ESP_OK = 0;
struct esp_partition_t {
  size_t size;
};
const esp_partition_t* esp_ota_get_next_update_partition(const void*);
esp_err_t esp_ota_begin(const esp_partition_t*, size_t, esp_ota_handle_t*);
esp_err_t esp_ota_write(esp_ota_handle_t, const void*, size_t);
esp_err_t esp_ota_abort(esp_ota_handle_t);
esp_err_t esp_ota_end(esp_ota_handle_t);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t*);
const char* esp_err_to_name(esp_err_t);
