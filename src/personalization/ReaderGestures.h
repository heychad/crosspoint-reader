#pragma once

#include <array>
#include <cstdint>

#include "EdgeSlide.h"
#include "ReaderGestureBindings.h"
#include "ReaderPinchGesture.h"
#include "SwipeAdjustment.h"
#include "TwoFingerSwipe.h"

namespace readergestures {
using Action = readershortcuts::Action;
struct Point {
  int x = 0;
  int y = 0;
  uint8_t id = 0;
  bool valid = false;
};
struct Frame {
  std::array<Point, 2> points{};
  uint8_t count = 0, reportedCount = 0, orientation = 0;
  bool idsStable = false;
  bool multiSupported = false;
  uint32_t now = 0;
  int width = 0, height = 0;
  bool swipe = false, rotation = false, pinch = false;
  TwoFingerSwipe::CompletedSwipe completedSwipe{};
  float rotationDegrees = 0, pinchScale = 1;
  bool singleSwipe = false;
  int endX = 0, endY = 0;
};
struct Config {
  bool enabled = false, pinch = true, rotation = true;
  std::array<uint8_t, 4> multi{};
  std::array<Action, 4> edges{};
};
enum class EventKind : uint8_t { Discrete, LightBegin, LightUpdate, LightFinish, LightCancel };
struct Event {
  EventKind kind = EventKind::Discrete;
  Action action = Action::Default;
  int amount = 0;
};
struct Result {
  std::array<Event, 2> events{};
  uint8_t count = 0;
  bool consumed = false;
  bool started = false;
  bool suppressContact = false;
  void add(EventKind kind, Action action = Action::Default, int amount = 0) {
    if (count < events.size()) events[count++] = {kind, action, amount};
  }
};

inline bool isLight(Action action) {
  return action == Action::Brighter || action == Action::Dimmer || action == Action::Warmer || action == Action::Cooler;
}

// Main-loop-owned, allocation-free recognition. A contact must begin in this reader.
class Controller {
 public:
  Result update(const Frame& frame, const Config& config);
  Result cancel(bool contactsDown);
  // Pass unclaimed contacts through while unavailable; roll back an owned drag.
  Result suspend(bool contactsDown, bool ineligible);

 private:
  enum class Phase : uint8_t {
    Idle,
    Single,
    Multi,
    EdgeAction,
    EdgeLight,
    MultiLight,
    Consumed,
    Blocked,
    Ineligible,
    Bypassed
  };
  void beginSingle(const Frame& frame);
  void beginMulti(const Frame& frame);
  void finishSequence(bool contactsDown);
  bool sameIds(const Frame& frame) const;
  Result single(const Frame& frame, const Config& config);
  Result multi(const Frame& frame, const Config& config);
  static Action edgeAction(EdgeSlide::Direction direction, const Config& config);
  static Action multiAction(TwoFingerSwipe::Direction direction, const Config& config);
  void cancelLight(Result& result);
  Phase phase = Phase::Blocked;
  ReaderPinchGesture pinch;
  Point first{}, last{};
  TwoFingerSwipe::FingerPair pair{};
  std::array<uint8_t, 2> ids{};
  bool stableIds = false, lightBlocked = false;
  uint32_t startTime = 0;
  int width = 0, height = 0;
  uint8_t orientation = 0;
  Action activeAction = Action::Default;
  TwoFingerSwipe::Direction activeDirection = TwoFingerSwipe::Direction::None;
};
static_assert(sizeof(Controller) <= 256, "Review gesture state growth");
}  // namespace readergestures
