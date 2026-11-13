#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>

#include "LegacyHomeButtonShortcuts.h"
#include "LegacyPowerShortcuts.h"
#include "LegacySideShortcuts.h"
#include "ReaderGestureJson.h"

namespace crossinkimport {
inline bool stageGestureControls(JsonVariantConst legacy, JsonDocument& proposed, bool replaceExisting) {
  bool changed = false;
  const auto sourceDisabled = legacy["disableReaderTouchscreen"];
  const auto sourceTouch = legacy["touchReaderControls"];
  const auto currentTouch = proposed["touchReaderControls"];
  if (sourceDisabled.is<uint8_t>() && sourceDisabled.as<uint8_t>() <= 1 &&
      (replaceExisting || currentTouch.isUnbound())) {
    const bool disabled = sourceDisabled.as<uint8_t>() != 0;
    const bool sourceTouchKnown = sourceTouch.is<uint8_t>() && sourceTouch.as<uint8_t>() <= 1;
    if (disabled || sourceTouchKnown || sourceTouch.isUnbound()) {
      // RC2 saves only the disable flag; older files can also carry a touch mode.
      const uint8_t value = disabled ? 0 : sourceTouchKnown ? sourceTouch.as<uint8_t>() : 1;
      bool quickOnEdge = false;
      for (const char* key : {"readerLeftEdgeUp", "readerLeftEdgeDown", "readerRightEdgeUp", "readerRightEdgeDown"}) {
        const auto edge = proposed[key];
        if (edge.is<uint8_t>() && edge.as<uint8_t>() == static_cast<uint8_t>(readershortcuts::Action::QuickActions))
          quickOnEdge = true;
      }
      if ((value != 0 || !quickOnEdge) && !(currentTouch.is<uint8_t>() && currentTouch.as<uint8_t>() == value)) {
        proposed["touchReaderControls"] = value;
        changed = true;
      }
    }
  }
  for (const char* key : {"pinchFontResizeEnabled", "twoFingerRotationEnabled", "disablePageTurnGestures"}) {
    const auto source = legacy[key];
    const auto current = proposed[key];
    if (!source.is<uint8_t>() || source.as<uint8_t>() > 1 || (!replaceExisting && !current.isUnbound())) continue;
    const uint8_t value = source.as<uint8_t>();
    if (current.is<uint8_t>() && current.as<uint8_t>() == value) continue;
    proposed[key] = value;
    changed = true;
  }

  const auto nextGesture = legacy["pageTurnGesture"];
  static constexpr std::array<const char*, 2> pageKeys = {"pageTurnGesture", "previousPageGesture"};
  for (size_t i = 0; i < pageKeys.size(); ++i) {
    const char* key = pageKeys[i];
    auto source = legacy[key];
    if (i == 1 && source.isNull()) source = nextGesture;
    const auto current = proposed[key];
    if (!source.is<uint8_t>() || source.as<uint8_t>() > 4 || (!replaceExisting && !current.isUnbound())) continue;
    const uint8_t value = source.as<uint8_t>();
    if (current.is<uint8_t>() && current.as<uint8_t>() == value) continue;
    proposed[key] = value;
    changed = true;
  }

  constexpr uint8_t unmapped = UINT8_MAX;
  std::array<uint8_t, 4> edges{unmapped, unmapped, unmapped, unmapped};
  readergestures::readEmbeddedLegacyEdges(legacy, {&edges[0], &edges[1], &edges[2], &edges[3]});
  static constexpr std::array<const char*, 4> edgeKeys = {"readerLeftEdgeUp", "readerLeftEdgeDown", "readerRightEdgeUp",
                                                          "readerRightEdgeDown"};
  for (size_t i = 0; i < edges.size(); ++i) {
    if (edges[i] == unmapped) continue;
    const auto current = proposed[edgeKeys[i]];
    if (!replaceExisting && !current.isUnbound()) continue;
    if (current.is<uint8_t>() && current.as<uint8_t>() == static_cast<uint8_t>(readershortcuts::Action::QuickActions))
      continue;
    if (current.is<uint8_t>() && current.as<uint8_t>() == edges[i]) continue;
    proposed[edgeKeys[i]] = edges[i];
    changed = true;
  }

  // The reader requires nonzero two-finger actions to be unique. Stage the
  // group only when its final values do not silently clear another binding.
  std::array<uint8_t, 4> values{};
  std::array<bool, 4> apply{};
  bool valid = true;
  for (size_t i = 0; i < values.size(); ++i) {
    const auto current = proposed[readergestures::KEYS[i]];
    const auto source = legacy[readergestures::KEYS[i]];
    if (!current.isUnbound()) {
      if (!current.is<uint8_t>() || current.as<uint8_t>() >= static_cast<uint8_t>(readergestures::Binding::Count)) {
        valid = false;
        break;
      }
      values[i] = current.as<uint8_t>();
    }
    if ((replaceExisting || current.isUnbound()) && source.is<uint8_t>() &&
        source.as<uint8_t>() < static_cast<uint8_t>(readergestures::Binding::Count)) {
      values[i] = source.as<uint8_t>();
      apply[i] = true;
    }
  }
  for (size_t i = 0; valid && i < values.size(); ++i)
    for (size_t j = i + 1; j < values.size(); ++j)
      if (values[i] != 0 && values[i] == values[j]) valid = false;
  if (valid) {
    for (size_t i = 0; i < values.size(); ++i) {
      if (!apply[i]) continue;
      const auto current = proposed[readergestures::KEYS[i]];
      if (current.is<uint8_t>() && current.as<uint8_t>() == values[i]) continue;
      proposed[readergestures::KEYS[i]] = values[i];
      changed = true;
    }
  }
  return changed;
}

// Stages a controls import in a caller-owned document. The caller must discard
// it on overflow and is responsible for confirmation, locking, and saving.
inline bool stageControls(JsonVariantConst legacy, JsonDocument& proposed, bool replaceExisting,
                          bool currentQuickTrigger) {
  if (!legacy.is<JsonObjectConst>() || !proposed.is<JsonObject>()) return false;
  constexpr uint8_t UNMAPPED = UINT8_MAX;
  std::array<uint8_t, 5> side{UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED, UNMAPPED};
  const std::array<uint8_t*, 5> destinations = {&side[0], &side[1], &side[2], &side[3], &side[4]};
  uint8_t orientation = 0;
  bool orientationCompatible = readershortcuts::legacySideOrientation(legacy, orientation);
  const auto currentOrientation = proposed["readerSideFollowOrientation"];
  const bool currentMatches = (currentOrientation.is<uint8_t>() && currentOrientation.as<uint8_t>() == orientation) ||
                              (currentOrientation.isUnbound() && orientation == 0);
  bool changed = stageGestureControls(legacy, proposed, replaceExisting);
  if (orientationCompatible && !currentMatches) {
    if ((!replaceExisting && !currentOrientation.isUnbound()) || currentQuickTrigger) {
      orientationCompatible = false;
    } else {
      proposed["readerSideFollowOrientation"] = orientation;
      changed = true;
    }
  }
  if (orientationCompatible) readershortcuts::readEmbeddedLegacySides(legacy, destinations);
  readerquickactions::Slots slots{};
  const bool slotsMapped = readershortcuts::readEmbeddedLegacyQuickSlots(legacy, slots);
  const bool sourceHasSlots = !legacy["quickActionSlots"].isUnbound();
  if (orientationCompatible)
    readershortcuts::readEmbeddedLegacySideQuickAction(legacy, destinations,
                                                       currentQuickTrigger || (sourceHasSlots && !slotsMapped));

  static constexpr std::array<const char*, 5> keys = {"readerUpTap", "readerUpHold", "readerDownTap", "readerDownHold",
                                                      "readerSideChord"};
  for (size_t i = 0; i < keys.size(); ++i) {
    if (side[i] == UNMAPPED) continue;
    const auto current = proposed[keys[i]];
    if (!replaceExisting && !current.isUnbound()) continue;
    // A controls import must never remove the current Quick Actions owner.
    if (current.is<uint8_t>() && current.as<uint8_t>() == static_cast<uint8_t>(readershortcuts::Action::QuickActions) &&
        side[i] != static_cast<uint8_t>(readershortcuts::Action::QuickActions))
      continue;
    if (current.is<uint8_t>() && current.as<uint8_t>() == side[i]) continue;
    proposed[keys[i]] = side[i];
    changed = true;
  }
  if (slotsMapped && (replaceExisting || proposed["readerQuickActionsV1"].isUnbound())) {
    const auto current = proposed["readerQuickActionsV1"].as<JsonArrayConst>();
    bool same = !current.isNull() && current.size() == slots.size();
    for (size_t i = 0; same && i < slots.size(); ++i)
      same = current[i].is<uint8_t>() && current[i].as<uint8_t>() == slots[i];
    if (!same) {
      readerquickactions::write(proposed, slots);
      changed = true;
    }
  }
  std::array<uint8_t, 3> power{UNMAPPED, UNMAPPED, UNMAPPED};
  readershortcuts::Action tap;
  if (readershortcuts::readLegacyPowerTap(legacy, tap)) power[0] = static_cast<uint8_t>(tap);
  readershortcuts::readEmbeddedLegacyPower(legacy, {&power[1], &power[2]});
  const auto legacyTrigger = legacy["quickActionsTrigger"];
  const uint8_t trigger = legacyTrigger.is<uint8_t>() ? legacyTrigger.as<uint8_t>() : 0;
  const bool importQuick = !currentQuickTrigger && (!sourceHasSlots || slotsMapped) &&
                           readershortcuts::uniqueLegacyQuickOwner(legacy, trigger);
  if (importQuick && (trigger == 1 || trigger == 2 || trigger == 5)) {
    const size_t index = trigger == 1 ? 0 : trigger == 2 ? 1 : 2;
    power[index] = static_cast<uint8_t>(readershortcuts::Action::QuickActions);
  }
  static constexpr std::array<const char*, 3> powerKeys = {"powerTapAction", "powerHoldAction", "powerUpChordAction"};
  for (size_t i = 0; i < power.size(); ++i) {
    if (power[i] == UNMAPPED) continue;
    const auto current = proposed[powerKeys[i]];
    if (!replaceExisting && !current.isUnbound()) continue;
    if (current.is<uint8_t>() && current.as<uint8_t>() == static_cast<uint8_t>(readershortcuts::Action::QuickActions))
      continue;
    if (current.is<uint8_t>() && current.as<uint8_t>() == power[i]) continue;
    proposed[powerKeys[i]] = power[i];
    changed = true;
  }
  static constexpr std::array<const char*, 3> homeKeys = {"homeButtonTapAction", "homeButtonLongPressAction",
                                                          "homeButtonDoubleTapAction"};
  const auto sourceHomeEnabled = legacy["homeButtonInReaderEnabled"];
  const auto currentHomeEnabled = proposed["homeButtonInReaderEnabled"];
  bool homeOwnsQuickActions = false;
  for (const auto* key : homeKeys) {
    const auto action = proposed[key];
    homeOwnsQuickActions |=
        action.is<uint8_t>() && action.as<uint8_t>() == static_cast<uint8_t>(HomeButtonAction::QuickActions);
  }
  if (sourceHomeEnabled.is<uint8_t>() && sourceHomeEnabled.as<uint8_t>() <= 1 &&
      (replaceExisting || currentHomeEnabled.isUnbound()) &&
      (sourceHomeEnabled.as<uint8_t>() != 0 || !homeOwnsQuickActions) &&
      (!currentHomeEnabled.is<uint8_t>() || currentHomeEnabled.as<uint8_t>() != sourceHomeEnabled.as<uint8_t>())) {
    proposed["homeButtonInReaderEnabled"] = sourceHomeEnabled.as<uint8_t>();
    changed = true;
  }
  const auto effectiveHomeEnabled = proposed["homeButtonInReaderEnabled"];
  if (sourceHomeEnabled.is<uint8_t>() && sourceHomeEnabled.as<uint8_t>() == 1 &&
      (effectiveHomeEnabled.isUnbound() ||
       (effectiveHomeEnabled.is<uint8_t>() && effectiveHomeEnabled.as<uint8_t>() == 1))) {
    for (size_t i = 0; i < homeKeys.size(); ++i) {
      const auto source = legacy[homeKeys[i]];
      const auto current = proposed[homeKeys[i]];
      if ((!replaceExisting && !current.isUnbound()) || !source.is<uint8_t>()) continue;
      HomeButtonAction action;
      if (importQuick && trigger == i + 6)
        action = HomeButtonAction::QuickActions;
      else if (!readershortcuts::readLegacyHomeAction(source.as<uint8_t>(), action))
        continue;
      const auto value = static_cast<uint8_t>(action);
      if (current.is<uint8_t>() && current.as<uint8_t>() == static_cast<uint8_t>(HomeButtonAction::QuickActions))
        continue;
      if (current.is<uint8_t>() && current.as<uint8_t>() == value) continue;
      proposed[homeKeys[i]] = value;
      changed = true;
    }
  }
  return changed && !proposed.overflowed();
}
}  // namespace crossinkimport
