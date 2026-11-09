#pragma once
#include <cstddef>
#include <cstdint>
struct ZipFile {
  static uint64_t fnvHash64(const char* data, size_t size) {
    uint64_t hash = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) {
      hash ^= static_cast<uint8_t>(data[i]);
      hash *= 1099511628211ull;
    }
    return hash;
  }
};
