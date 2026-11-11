#pragma once
#include <cstdint>

namespace fake {
inline bool failSecret = false;
inline bool failExistingSecret = false;
inline unsigned createCalls = 0;
inline unsigned readOnlyCalls = 0;
}  // namespace fake
inline bool deviceSecret(uint8_t (&out)[32]) {
  ++fake::createCalls;
  if (fake::failSecret) return false;
  for (unsigned i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(i);
  return true;
}
inline bool existingDeviceSecret(uint8_t (&out)[32]) {
  ++fake::readOnlyCalls;
  if (fake::failExistingSecret) return false;
  for (unsigned i = 0; i < 32; ++i) out[i] = static_cast<uint8_t>(i);
  return true;
}
