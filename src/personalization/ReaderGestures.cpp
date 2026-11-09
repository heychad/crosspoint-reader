#include "ReaderGestures.h"

#include <algorithm>
#include <cstdlib>

namespace readergestures {
void Controller::finishSequence(bool contactsDown) {
  phase = contactsDown ? Phase::Consumed : Phase::Idle;
  pinch.reset();
  lightBlocked = false;
  activeAction = Action::Default;
}

void Controller::cancelLight(Result& result) {
  if (phase == Phase::EdgeLight || phase == Phase::MultiLight) result.add(EventKind::LightCancel, activeAction);
}

Result Controller::cancel(bool contactsDown) {
  Result result;
  if (phase == Phase::EdgeLight || phase == Phase::MultiLight) result.add(EventKind::LightFinish, activeAction);
  result.consumed = phase != Phase::Idle && phase != Phase::Blocked;
  result.suppressContact = result.consumed;
  finishSequence(contactsDown);
  phase = contactsDown ? Phase::Consumed : Phase::Idle;
  return result;
}

Result Controller::suspend(bool contactsDown, bool ineligible) {
  Result result;
  const bool owned = (phase == Phase::Blocked && contactsDown) || phase == Phase::Multi || phase == Phase::MultiLight ||
                     phase == Phase::EdgeAction || phase == Phase::EdgeLight || phase == Phase::Consumed;
  ineligible = ineligible || phase == Phase::Ineligible;
  if (ineligible && (phase == Phase::EdgeLight || phase == Phase::MultiLight)) {
    result.add(EventKind::LightFinish, activeAction);
  } else {
    cancelLight(result);
  }
  result.consumed = owned;
  result.suppressContact = owned;
  finishSequence(contactsDown && owned);
  if (!owned && contactsDown) phase = ineligible ? Phase::Ineligible : Phase::Bypassed;
  return result;
}

void Controller::beginSingle(const Frame& frame) {
  phase = Phase::Single;
  first = last = frame.points[0];
  startTime = frame.now;
  width = frame.width;
  height = frame.height;
  orientation = frame.orientation;
  stableIds = frame.idsStable;
}

void Controller::beginMulti(const Frame& frame) {
  phase = Phase::Multi;
  pair = {frame.points[0].x, frame.points[0].y, frame.points[1].x, frame.points[1].y};
  ids = {frame.points[0].id, frame.points[1].id};
  stableIds = frame.idsStable;
  startTime = frame.now;
  width = frame.width;
  height = frame.height;
  orientation = frame.orientation;
  lightBlocked = false;
  pinch.reset();
  pinch.update(pair.firstX, pair.firstY, pair.secondX, pair.secondY);
}

bool Controller::sameIds(const Frame& frame) const {
  if (!frame.count) return true;
  if (stableIds != frame.idsStable) return false;
  if (!stableIds) return true;
  if (frame.count == 1) return frame.points[0].id == ids[0] || frame.points[0].id == ids[1];
  return (frame.points[0].id == ids[0] && frame.points[1].id == ids[1]) ||
         (frame.points[0].id == ids[1] && frame.points[1].id == ids[0]);
}

Action Controller::edgeAction(EdgeSlide::Direction direction, const Config& config) {
  switch (direction) {
    case EdgeSlide::Direction::LeftUp:
      return config.edges[0];
    case EdgeSlide::Direction::LeftDown:
      return config.edges[1];
    case EdgeSlide::Direction::RightUp:
      return config.edges[2];
    case EdgeSlide::Direction::RightDown:
      return config.edges[3];
    case EdgeSlide::Direction::None:
      return Action::Default;
  }
  return Action::Default;
}

Action Controller::multiAction(TwoFingerSwipe::Direction direction, const Config& config) {
  switch (direction) {
    case TwoFingerSwipe::Direction::Up:
      return actionFor(config.multi[0]);
    case TwoFingerSwipe::Direction::Down:
      return actionFor(config.multi[1]);
    case TwoFingerSwipe::Direction::Left:
      return actionFor(config.multi[2]);
    case TwoFingerSwipe::Direction::Right:
      return actionFor(config.multi[3]);
    case TwoFingerSwipe::Direction::None:
      return Action::Default;
  }
  return Action::Default;
}

Result Controller::single(const Frame& frame, const Config& config) {
  Result result;
  if (frame.count == 1)
    last = frame.points[0];
  else if (frame.singleSwipe) {
    last.x = frame.endX;
    last.y = frame.endY;
  }
  const bool finished = frame.count == 0;
  const int band = EdgeSlide::bandWidth(width);
  const bool inBand = first.x < band ? last.x < band : last.x >= width - band;
  if (phase == Phase::EdgeLight) {
    result.consumed = true;
    const int displacement = activeDirection == TwoFingerSwipe::Direction::Up ? first.y - last.y : last.y - first.y;
    if (inBand)
      result.add(EventKind::LightUpdate, activeAction, SwipeAdjustment::amount(std::max(0, displacement), height));
    if (finished || !inBand) {
      result.add(EventKind::LightFinish, activeAction);
      finishSequence(!finished);
      result.suppressContact = true;
    }
    return result;
  }
  const auto direction = EdgeSlide::directionFor(first.x, first.y, last.x, last.y, width, height);
  const auto action = edgeAction(direction, config);
  if (phase == Phase::EdgeAction) {
    result.consumed = true;
    if (!inBand) {
      finishSequence(!finished);
      result.suppressContact = true;
      return result;
    }
    if (finished) {
      if (direction != EdgeSlide::Direction::None) result.add(EventKind::Discrete, action);
      finishSequence(false);
      result.suppressContact = true;
    }
    return result;
  }
  if (action != Action::Default && direction != EdgeSlide::Direction::None) {
    result.consumed = true;
    result.started = true;
    activeAction = action;
    activeDirection = last.y < first.y ? TwoFingerSwipe::Direction::Up : TwoFingerSwipe::Direction::Down;
    if (isLight(action)) {
      result.add(EventKind::LightBegin, action, SwipeAdjustment::amount(std::abs(last.y - first.y), height));
      phase = Phase::EdgeLight;
      if (finished) {
        result.add(EventKind::LightFinish, action);
        finishSequence(false);
      }
      result.suppressContact = true;
    } else if (finished) {
      result.add(EventKind::Discrete, action);
      result.suppressContact = true;
      finishSequence(false);
    } else
      phase = Phase::EdgeAction;
  } else if (finished)
    finishSequence(false);
  return result;
}

Result Controller::multi(const Frame& frame, const Config& config) {
  Result result;
  result.consumed = true;
  if (!sameIds(frame)) {
    cancelLight(result);
    finishSequence(true);
    result.suppressContact = true;
    return result;
  }
  if (frame.count < 2) {
    if (frame.rotation) {
      cancelLight(result);
      if (config.rotation)
        result.add(EventKind::Discrete, frame.rotationDegrees < 0 ? Action::Rotate : Action::RotateBackward);
    } else if (frame.pinch && config.pinch) {
      cancelLight(result);
      result.add(EventKind::Discrete, frame.pinchScale > 1 ? Action::LargerText : Action::SmallerText);
    } else if (phase == Phase::MultiLight) {
      result.add(EventKind::LightFinish, activeAction);
    } else if (frame.swipe && !lightBlocked) {
      const auto direction = TwoFingerSwipe::directionFor(frame.completedSwipe, width, height);
      const auto action = multiAction(direction, config);
      if (action != Action::Default) {
        if (isLight(action)) {
          const bool vertical =
              direction == TwoFingerSwipe::Direction::Up || direction == TwoFingerSwipe::Direction::Down;
          const int distance = vertical ? std::abs(frame.completedSwipe.endY - frame.completedSwipe.startY)
                                        : std::abs(frame.completedSwipe.endX - frame.completedSwipe.startX);
          result.add(EventKind::LightBegin, action, SwipeAdjustment::amount(distance, vertical ? height : width));
          result.add(EventKind::LightFinish, action);
        } else
          result.add(EventKind::Discrete, action);
      }
    }
    result.suppressContact = true;
    finishSequence(frame.count != 0);
    return result;
  }
  const TwoFingerSwipe::FingerPair current =
      TwoFingerSwipe::alignedPair(pair, {frame.points[0].x, frame.points[0].y, frame.points[1].x, frame.points[1].y});
  if (config.pinch) {
    const auto resize = pinch.update(current.firstX, current.firstY, current.secondX, current.secondY);
    if (resize != ReaderPinchGesture::Action::None) {
      cancelLight(result);
      result.add(EventKind::Discrete,
                 resize == ReaderPinchGesture::Action::Increase ? Action::LargerText : Action::SmallerText);
      result.suppressContact = true;
      finishSequence(true);
      return result;
    }
  }
  if (!TwoFingerSwipe::hasTranslationGeometry(pair, current)) {
    cancelLight(result);
    phase = Phase::Multi;
    lightBlocked = true;
    return result;
  }
  const int centerX = (current.firstX + current.secondX) / 2;
  const int centerY = (current.firstY + current.secondY) / 2;
  const int startX = (pair.firstX + pair.secondX) / 2;
  const int startY = (pair.firstY + pair.secondY) / 2;
  if (phase != Phase::MultiLight && !lightBlocked) {
    const TwoFingerSwipe::CompletedSwipe movement{2, startX, startY, centerX, centerY, frame.now - startTime};
    const auto direction = TwoFingerSwipe::directionFor(movement, width, height);
    const auto action = multiAction(direction, config);
    if (!isLight(action) || !TwoFingerSwipe::fingersMovedTogether(pair, current, direction)) return result;
    phase = Phase::MultiLight;
    activeDirection = direction;
    activeAction = action;
    const bool vertical = direction == TwoFingerSwipe::Direction::Up || direction == TwoFingerSwipe::Direction::Down;
    result.add(EventKind::LightBegin, action,
               SwipeAdjustment::amount(vertical ? std::abs(centerY - startY) : std::abs(centerX - startX),
                                       vertical ? height : width));
    return result;
  }
  if (phase == Phase::MultiLight) {
    const bool vertical =
        activeDirection == TwoFingerSwipe::Direction::Up || activeDirection == TwoFingerSwipe::Direction::Down;
    const int sign =
        activeDirection == TwoFingerSwipe::Direction::Up || activeDirection == TwoFingerSwipe::Direction::Left ? -1 : 1;
    const int distance = sign * (vertical ? centerY - startY : centerX - startX);
    result.add(EventKind::LightUpdate, activeAction,
               SwipeAdjustment::amount(std::max(0, distance), vertical ? height : width));
  }
  return result;
}

Result Controller::update(const Frame& frame, const Config& config) {
  if (!config.enabled || frame.width < 2 || frame.height < 2) return suspend(frame.count != 0, true);
  if (phase == Phase::Ineligible) {
    Result result;
    result.consumed = result.suppressContact = true;
    finishSequence(frame.count != 0);
    return result;
  }
  if (phase == Phase::Bypassed) {
    if (!frame.count) finishSequence(false);
    return {};
  }
  if (phase == Phase::Blocked || phase == Phase::Consumed) {
    Result result;
    result.consumed = frame.count != 0 || phase == Phase::Consumed;
    if (!frame.count)
      finishSequence(false);
    else {
      phase = Phase::Consumed;
      result.suppressContact = true;
    }
    return result;
  }
  if (phase != Phase::Idle && (frame.width != width || frame.height != height || frame.orientation != orientation)) {
    auto result = cancel(frame.count != 0);
    result.consumed = true;
    return result;
  }
  if (frame.reportedCount > 2 || frame.count > 2 || frame.count != frame.reportedCount ||
      (frame.count && !frame.points[0].valid) || (frame.count == 2 && !frame.points[1].valid) ||
      (frame.count == 2 && frame.idsStable && frame.points[0].id == frame.points[1].id)) {
    auto result = cancel(frame.count != 0);
    result.consumed = true;
    result.suppressContact = true;
    return result;
  }
  if ((phase == Phase::Single || phase == Phase::EdgeAction || phase == Phase::EdgeLight) && frame.count == 1 &&
      (stableIds != frame.idsStable || (stableIds && first.id != frame.points[0].id))) {
    Result result;
    cancelLight(result);
    result.consumed = result.suppressContact = true;
    finishSequence(true);
    return result;
  }
  bool started = false;
  if (phase == Phase::Idle) {
    if (!frame.count) return {};
    if (frame.count == 2 && frame.multiSupported) {
      beginMulti(frame);
      started = true;
    } else if (frame.count == 1)
      beginSingle(frame);
    else
      return cancel(true);
  } else if ((phase == Phase::Single || phase == Phase::EdgeAction) && frame.count == 2 && frame.multiSupported) {
    beginMulti(frame);
    started = true;
  } else if (phase == Phase::EdgeLight && frame.count == 2) {
    auto result = cancel(true);
    result.consumed = true;
    result.suppressContact = true;
    return result;
  }
  if (phase == Phase::Multi || phase == Phase::MultiLight) {
    auto result = multi(frame, config);
    result.started = started;
    return result;
  }
  return single(frame, config);
}
}  // namespace readergestures
