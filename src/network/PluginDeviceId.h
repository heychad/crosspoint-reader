#pragma once

#include <cstddef>

namespace pluginidentity {
bool deviceId(const char* plugin, size_t length, char (&hex)[65], bool create = true);
}  // namespace pluginidentity
