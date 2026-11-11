#include "StatsSaveNotice.h"

#include <esp_attr.h>

namespace {
// No initializer: setup() clears this on cold boot and validates it on wake.
RTC_NOINIT_ATTR uint32_t retainedStatsSaveNotice;
RTC_NOINIT_ATTR uint32_t retainedSettingsSaveNotice;
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
RTC_NOINIT_ATTR uint32_t retainedStateSaveNotice;
#endif
}  // namespace
StatsSaveNotice& statsSaveNotice() {
  static StatsSaveNotice notice(retainedStatsSaveNotice);
  return notice;
}
StatsSaveNotice& settingsSaveNotice() {
  static StatsSaveNotice notice(retainedSettingsSaveNotice);
  return notice;
}
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
StatsSaveNotice& stateSaveNotice() {
  static StatsSaveNotice notice(retainedStateSaveNotice);
  return notice;
}
#endif
