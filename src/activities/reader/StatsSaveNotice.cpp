#include "StatsSaveNotice.h"

#include <esp_attr.h>

namespace {
// No initializer: setup() clears this on cold boot and validates it on wake.
RTC_NOINIT_ATTR uint32_t retainedStatsSaveNotice;
}  // namespace
StatsSaveNotice& statsSaveNotice() {
  static StatsSaveNotice notice(retainedStatsSaveNotice);
  return notice;
}
