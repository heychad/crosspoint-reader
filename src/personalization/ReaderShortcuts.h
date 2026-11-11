#pragma once

#include <array>
#include <cstdint>

#include "util/SideButtonPolicy.h"

namespace readershortcuts {
enum class Action : uint8_t {
  Default,
  Disabled,
  PreviousPage,
  NextPage,
  PreviousChapter,
  NextChapter,
  Menu,
  Bookmark,
  Dictionary,
  Footnotes,
  Refresh,
  ToggleFrontlight,
  Brighter,
  Dimmer,
  Rotate,
  RotateBackward,
  Warmer,
  Cooler,
  LargerText,
  SmallerText,
  RotateHalfTurn,
  QuickActions,
  Home,
  FileBrowser,
  Library,
  ReadingStatistics,
  Screenshot,
  SyncProgress,
  CreateClipping,
  ToggleNight,
  ToggleFocus,
  ToggleTouch,
  ToggleTilt,
  CycleAutoTurn,
  QuickLock,
  Sleep,
  SavedItems,
  FileTransfer,
  FontFamily,
  CycleFontFamily,
  ToggleHomeButton,
  ToggleCompletion,
  HomeReader,
  SelectChapter,
  Count
};
// The existing edge palette ends before physical-only lock/sleep actions.
inline constexpr uint8_t EDGE_ACTION_COUNT = static_cast<uint8_t>(Action::QuickLock);
enum class Trigger : uint8_t { None, UpTap, UpHold, DownTap, DownHold, Chord };
inline bool swapSideBindings(bool followsOrientation, uint8_t orientation) {
  return followsOrientation && orientation != 0;
}
inline bool logicalSideUp(bool physicalUp, bool swapped) { return physicalUp != swapped; }
struct Contact {
  bool down = false;
  bool pressed = false;
  bool released = false;
};
struct Event {
  Trigger trigger = Trigger::None;
  bool managed = false;
};

// A press belongs to the activity that first saw its edge. Holds fire once and
// suppress their release; a chord consumes both contacts until both are up.
class Tracker {
 public:
  Event update(uint32_t now, std::array<Contact, 2> contacts, bool managed, std::array<bool, 2> hasHold,
               bool chordEnabled) {
    if (!managed) {
      reset();
      return {};
    }
    if (chordActive) {
      if (!contacts[0].down && !contacts[1].down) chordActive = false;
      return {Trigger::None, true};
    }
    if (chordEnabled && contacts[0].down && contacts[1].down && (state[0].tracking || contacts[0].pressed) &&
        (state[1].tracking || contacts[1].pressed)) {
      state = {};
      chordActive = true;
      return {Trigger::Chord, true};
    }
    Event result{Trigger::None, true};
    for (unsigned i = 0; i < state.size(); ++i) {
      auto& s = state[i];
      const auto c = contacts[i];
      if (c.pressed) {
        s = {now, true, false};
      }
      if (!s.tracking) continue;
      if (!s.held && hasHold[i] && now - s.at >= HOLD_MS && (c.down || c.released)) {
        s.held = true;
        if (result.trigger == Trigger::None) result.trigger = i == 0 ? Trigger::UpHold : Trigger::DownHold;
      } else if (c.released && !s.held) {
        if (result.trigger == Trigger::None) result.trigger = i == 0 ? Trigger::UpTap : Trigger::DownTap;
      }
      if (!c.down) s.tracking = false;
    }
    return result;
  }
  void reset() {
    state = {};
    chordActive = false;
  }
  static constexpr uint32_t HOLD_MS = 700;

 private:
  struct State {
    uint32_t at = 0;
    bool tracking = false;
    bool held = false;
  };
  std::array<State, 2> state{};
  bool chordActive = false;
};

inline Action pageAction(bool up, uint8_t layout, bool inverted) {
  switch (sidePageDirection(up, layout, inverted)) {
    case SidePageDirection::Previous:
      return Action::PreviousPage;
    case SidePageDirection::Next:
      return Action::NextPage;
    case SidePageDirection::None:
      return Action::Disabled;
  }
  return Action::Disabled;
}
inline Action defaultAction(bool up, bool hold, uint8_t layout, bool inverted, uint8_t holdMode) {
  const auto page = pageAction(up, layout, inverted);
  if (!hold || page == Action::Disabled) return page;
  switch (holdMode) {
    case 1:
      return page == Action::NextPage ? Action::NextChapter : Action::PreviousChapter;
    case 2:
      return page == Action::NextPage ? Action::RotateBackward : Action::Rotate;
    default:
      return Action::Disabled;
  }
}
}  // namespace readershortcuts
