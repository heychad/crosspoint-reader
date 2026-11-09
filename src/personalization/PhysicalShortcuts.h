#pragma once
#include <cstdint>
class ActivityManager;
class MappedInputManager;
class GfxRenderer;
class HalGPIO;
namespace physicalshortcuts {
enum class Outcome { Pass, Consumed, Handled, Sleep, SleepTimeout };
// Reads the already-polled input snapshot. Main-loop only; never polls hardware.
Outcome update(ActivityManager&, MappedInputManager&, HalGPIO&, GfxRenderer&, uint32_t now);
void beforeSleep(ActivityManager&);
bool overridesPowerTap();
bool overridesPowerHold();
}  // namespace physicalshortcuts
