#pragma once
#include <cstdint>
namespace faketransport {
inline int64_t floor = 0;
inline bool syncWorks = true;
inline unsigned syncCalls = 0;
}  // namespace faketransport
namespace trustedtime {
int64_t trustedNow();
bool syncNow(uint32_t timeout);
}  // namespace trustedtime
