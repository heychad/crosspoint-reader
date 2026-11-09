#pragma once

#include <cstdint>

// Persisted indices: append actions without reordering existing values.
enum class HomeButtonAction : uint8_t {
  Home,
  Ignore,
  NextPage,
  Refresh,
  Footnotes,
  Confirm,
  Sync,
  Bookmark,
  Dictionary,
  ReaderMenu,
  ToggleFrontlight,
  CreateClipping,
  QuickActions,
  QuickLock,
  FileBrowser,
  Library,
  SavedItems,
  FileTransfer,
  ReadingStatistics,
  Count
};

class HomeButtonInput {
 public:
  enum class Gesture : uint8_t { None, Tap, DoubleTap, LongPress };
  Gesture gesture() const { return lastGesture; }
  static constexpr uint32_t DOUBLE_TAP_MS = 350;

  HomeButtonAction update(uint32_t now, bool tapped, bool held, bool swiped, bool pressed, HomeButtonAction tap,
                          HomeButtonAction doubleTap, HomeButtonAction longPress, bool contactDown = false,
                          bool enabled = true) {
    lastGesture = Gesture::None;
    if (!enabled) {
      discardContact(contactDown);
      return HomeButtonAction::Ignore;
    }
    if (discardingContact) {
      reset();
      if (!contactDown) discardingContact = false;
      return HomeButtonAction::Ignore;
    }
    if (swiped) {
      reset();
      return HomeButtonAction::Ignore;
    }
    if (held) {
      reset();
      lastGesture = Gesture::LongPress;
      return longPress;
    }
    if (pending && !secondContact && now - tappedAt > DOUBLE_TAP_MS) {
      pending = tapped;
      tappedAt = now;
      lastGesture = Gesture::Tap;
      return tap;
    }
    if (pending && pressed) secondContact = true;
    if (!tapped) return HomeButtonAction::Ignore;
    if (doubleTap == HomeButtonAction::Ignore) {
      reset();
      lastGesture = Gesture::Tap;
      return tap;
    }
    if (pending) {
      reset();
      lastGesture = Gesture::DoubleTap;
      return doubleTap;
    }
    pending = true;
    tappedAt = now;
    return HomeButtonAction::Ignore;
  }

  void reset() {
    pending = false;
    secondContact = false;
    lastGesture = Gesture::None;
  }

  // A contact that began in the previous screen owns its remaining edges.
  void discardContact(bool contactDown) {
    reset();
    discardingContact = contactDown;
  }

 private:
  Gesture lastGesture = Gesture::None;
  uint32_t tappedAt = 0;
  bool pending = false;
  bool secondContact = false;
  bool discardingContact = false;
};
