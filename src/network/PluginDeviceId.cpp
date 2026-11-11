#include "PluginDeviceId.h"

#include <DeviceSecret.h>
#include <wolfssl/wolfcrypt/hash.h>

#include <cstdio>
#include <cstring>

namespace pluginidentity {
bool deviceId(const char* plugin, const size_t length, char (&hex)[65], const bool create) {
  uint8_t secret[32];
  if (!plugin || !length || length > 64 || !(create ? deviceSecret(secret) : existingDeviceSecret(secret)))
    return false;
  uint8_t input[32 + 64];
  memcpy(input, secret, sizeof(secret));
  memcpy(input + sizeof(secret), plugin, length);
  uint8_t hash[32];
  if (wc_Sha256Hash(input, sizeof(secret) + length, hash) != 0) return false;
  for (size_t i = 0; i < sizeof(hash); ++i) snprintf(hex + 2 * i, 3, "%02x", hash[i]);
  return true;
}
}  // namespace pluginidentity
