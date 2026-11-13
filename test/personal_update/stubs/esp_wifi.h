#pragma once
using wifi_ps_type_t = int;
using esp_err_t = int;
inline constexpr int WIFI_PS_NONE = 0, WIFI_PS_MIN_MODEM = 1, ESP_OK = 0;
namespace faketransport {
inline int powerSave = WIFI_PS_MIN_MODEM;
}
inline int esp_wifi_get_ps(wifi_ps_type_t* out) {
  *out = faketransport::powerSave;
  return ESP_OK;
}
inline int esp_wifi_set_ps(wifi_ps_type_t value) {
  faketransport::powerSave = value;
  return ESP_OK;
}
