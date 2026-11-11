#pragma once

#include <ArduinoJson.h>

#include <array>

#include "LegacySideShortcuts.h"

namespace readershortcuts {
inline bool readLegacyPowerAction(uint8_t raw, Action& action) {
  // Power bindings use the shared catalog through CrossInk 2.0 RC's action 40;
  // the side-button-only extension at 64 and above is not valid here.
  constexpr uint8_t LAST_POWER_ACTION = 40;
  if (raw > LAST_POWER_ACTION) return false;
  if (raw == 34) {
    action = Action::Sleep;
    return true;
  }
  return readLegacySideAction(raw, false, action);
}

// The short-power key also exists in native settings with different meanings,
// so this decoder is used only with an explicitly selected CrossInk source.
inline bool readLegacyPowerTap(JsonVariantConst doc, Action& action) {
  const auto value = doc["shortPwrBtn"];
  if (!value.is<uint8_t>()) return false;
  return readLegacyPowerAction(value.as<uint8_t>(), action);
}

// CrossInk stored long-power and Power+Up in keys not used by CrossPoint.
// Quick Actions needs separate ownership migration and is not mapped here.
inline bool readEmbeddedLegacyPower(JsonVariantConst doc, const std::array<uint8_t*, 2>& destinations) {
  static constexpr std::array<const char*, 2> legacy = {"longPwrBtn", "powerChordAction"};
  static constexpr std::array<const char*, 2> current = {"powerHoldAction", "powerUpChordAction"};
  bool imported = false;
  for (size_t i = 0; i < legacy.size(); ++i) {
    if (!doc[current[i]].isUnbound() || !doc[legacy[i]].is<uint8_t>()) continue;
    const uint8_t raw = doc[legacy[i]].as<uint8_t>();
    Action action;
    const bool mapped = i == 0 ? readLegacyPowerAction(raw, action) : readLegacySideAction(raw, true, action);
    if (!mapped) continue;
    *destinations[i] = static_cast<uint8_t>(action);
    imported = true;
  }
  return imported;
}

inline bool readEmbeddedLegacyPowerQuickAction(JsonVariantConst doc, const std::array<uint8_t*, 3>& destinations,
                                               bool hasCurrentTrigger) {
  if (hasCurrentTrigger || !doc["quickActionsTrigger"].is<uint8_t>()) return false;
  const uint8_t trigger = doc["quickActionsTrigger"].as<uint8_t>();
  if (trigger != 1 && trigger != 2 && trigger != 5) return false;
  const size_t selected = trigger == 1 ? 0 : trigger == 2 ? 1 : 2;
  static constexpr std::array<const char*, 3> current = {"powerTapAction", "powerHoldAction", "powerUpChordAction"};
  if (!doc[current[selected]].isUnbound() || !uniqueLegacyQuickOwner(doc, trigger)) return false;
  *destinations[selected] = static_cast<uint8_t>(Action::QuickActions);
  return true;
}
}  // namespace readershortcuts
