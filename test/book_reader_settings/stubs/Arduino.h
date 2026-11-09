#pragma once
#include <Print.h>

#include <cstdint>
#include <string>
inline uint32_t hostMillis = 0;
inline uint32_t millis() { return hostMillis; }
class String : public std::string {
 public:
  using std::string::string;
  size_t write(uint8_t value) {
    push_back(static_cast<char>(value));
    return 1;
  }
  size_t write(const uint8_t* data, size_t length) {
    append(reinterpret_cast<const char*>(data), length);
    return length;
  }
  bool isEmpty() const { return empty(); }
};
