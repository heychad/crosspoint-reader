#pragma once

#include <cstddef>

namespace settingsrecovery {
// Failed attempts can leave .tmp or .bak files for recovery.
bool writeSettings(const char* bytes, size_t size);
}  // namespace settingsrecovery
