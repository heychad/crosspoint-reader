#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace statsreplace {
// Replaces one small statistics file while retaining recovery artifacts on any
// uncertain I/O. A separate fallback file is required. Callers must treat a
// false result as needing review rather than deleting .tmp or .bak files.
bool save(const char* module, const std::string& path, const uint8_t* bytes, size_t size, bool retainBackup);
}  // namespace statsreplace
