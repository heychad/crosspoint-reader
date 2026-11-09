#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "ReaderShortcuts.h"

namespace readershortcuts {
// CrossInk stored the Home-button toggle as shortPwrBtn 26. A modern binding,
// even an explicit Default, takes precedence over the legacy field.
inline bool migrateLegacyHomePowerTap(JsonVariantConst doc, bool hasHomeKey, uint8_t& powerTap) {
  if (!hasHomeKey || !doc["powerTapAction"].isNull() || !doc["shortPwrBtn"].is<uint8_t>() ||
      doc["shortPwrBtn"].as<uint8_t>() != 26)
    return false;
  powerTap = static_cast<uint8_t>(Action::ToggleHomeButton);
  return true;
}
}  // namespace readershortcuts
