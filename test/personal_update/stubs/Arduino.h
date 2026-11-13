#pragma once
#include <cstdint>
namespace faketransport {
inline uint32_t now = 0;
}
inline uint32_t millis() { return faketransport::now; }
