#pragma once
#include "activities/ActivityManager.h"
namespace ScreenshotUtil {
inline void takeScreenshot(GfxRenderer& r) { ++r.screenshots; }
}  // namespace ScreenshotUtil
