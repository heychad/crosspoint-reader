#pragma once
#include <algorithm>
#include <cstdint>
namespace testmac {
inline int result = 0;
inline uint8_t address[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
}  // namespace testmac
inline int esp_efuse_mac_get_default(uint8_t* output) {
  std::copy_n(testmac::address, 6, output);
  return testmac::result;
}
