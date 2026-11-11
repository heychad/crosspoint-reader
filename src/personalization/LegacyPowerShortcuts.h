#pragma once

#include <ArduinoJson.h>

#include <array>

#include "LegacySideShortcuts.h"

namespace readershortcuts {
// The short-power key also exists in native settings with different meanings,
// so this decoder is used only with an explicitly selected CrossInk source.
inline bool readLegacyPowerTap(JsonVariantConst doc, Action& action) {
  const auto value = doc["shortPwrBtn"];
  if (!value.is<uint8_t>()) return false;
  const uint8_t raw = value.as<uint8_t>();
  if (raw == 34) {
    action = Action::Sleep;
    return true;
  }
  return raw < 37 && readLegacySideAction(raw, false, action);
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
    if (i == 0 && raw >= 37) continue;
    Action action;
    if (i == 0 && raw == 34)
      action = Action::Sleep;
    else if (!readLegacySideAction(raw, i == 1, action))
      continue;
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
