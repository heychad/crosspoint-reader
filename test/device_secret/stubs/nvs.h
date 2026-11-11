#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

using esp_err_t = int;
using nvs_handle_t = int;
inline constexpr esp_err_t ESP_OK = 0;
inline constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 1;
inline constexpr esp_err_t ESP_FAIL = 2;
inline constexpr int NVS_READONLY = 0;
inline constexpr int NVS_READWRITE = 1;

namespace fakeNvs {
inline bool nameExists = false;
inline bool secretExists = false;
inline bool failRead = false;
inline size_t length = 32;
inline uint8_t value[32] = {};
inline unsigned writes = 0;
inline unsigned commits = 0;
inline unsigned randomCalls = 0;
}  // namespace fakeNvs

inline esp_err_t nvs_open(const char*, int mode, nvs_handle_t* handle) {
  if (mode == NVS_READONLY && !fakeNvs::nameExists) return ESP_ERR_NVS_NOT_FOUND;
  if (mode == NVS_READWRITE) fakeNvs::nameExists = true;
  *handle = 1;
  return ESP_OK;
}
inline esp_err_t nvs_get_blob(nvs_handle_t, const char*, void* output, size_t* size) {
  if (fakeNvs::failRead) return ESP_FAIL;
  if (!fakeNvs::secretExists) return ESP_ERR_NVS_NOT_FOUND;
  *size = fakeNvs::length;
  std::memcpy(output, fakeNvs::value, fakeNvs::length);
  return ESP_OK;
}
inline esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void* input, size_t size) {
  ++fakeNvs::writes;
  fakeNvs::secretExists = true;
  fakeNvs::length = size;
  std::memcpy(fakeNvs::value, input, size);
  return ESP_OK;
}
inline esp_err_t nvs_commit(nvs_handle_t) {
  ++fakeNvs::commits;
  return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
