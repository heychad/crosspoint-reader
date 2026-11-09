#pragma once
#include "personalization/QuickLockTrigger.h"
#include "personalization/ReaderShortcuts.h"
class GfxRenderer {
 public:
  int screenshots = 0;
};
class ActivityManager {
 public:
  enum class ControlKind { None, Sleep, ToggleLock };
  struct ControlRequest {
    ControlKind kind = ControlKind::None;
    QuickLockTrigger trigger = QuickLockTrigger::None;
  };
  ControlRequest pending;
  bool locked = false, canLock = true, reader = true;
  bool isForegroundReader() const { return reader; }
  unsigned currentVisit = 0;
  int dispatched = 0;
  readershortcuts::Action last = readershortcuts::Action::Default;
  ControlRequest takeControlRequest() {
    auto r = pending;
    pending = {};
    return r;
  }
  bool canApplyInputLock() const { return canLock; }
  bool isInputLocked() const { return locked; }
  bool applyInputLock(bool value, bool = true) {
    locked = value;
    return true;
  }
  unsigned visit() const { return currentVisit; }
  bool dispatchShortcut(readershortcuts::Action a, QuickLockTrigger t) {
    ++dispatched;
    last = a;
    if (a == readershortcuts::Action::QuickLock)
      pending = {ControlKind::ToggleLock, t};
    else if (a == readershortcuts::Action::Sleep)
      pending = {ControlKind::Sleep, t};
    return true;
  }
};
