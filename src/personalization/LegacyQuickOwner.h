#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>

namespace readershortcuts {
struct LegacyQuickBinding {
  const char* key;
  uint8_t value;
};
// The CrossInk trigger number identifies one of these persisted keys.
inline bool uniqueLegacyQuickOwner(JsonVariantConst doc, uint8_t wanted) {
  static constexpr std::array<LegacyQuickBinding, 13> bindings = {{{"shortPwrBtn", 27},
                                                                   {"longPwrBtn", 27},
                                                                   {"longPressBackAction", 22},
                                                                   {"longPressMenuAction", 22},
                                                                   {"powerChordAction", 26},
                                                                   {"homeButtonTapAction", 27},
                                                                   {"homeButtonLongPressAction", 27},
                                                                   {"homeButtonDoubleTapAction", 27},
                                                                   {"sideButtonChordAction", 26},
                                                                   {"sideButtonUpShort", 27},
                                                                   {"sideButtonUpLong", 27},
                                                                   {"sideButtonDownShort", 27},
                                                                   {"sideButtonDownLong", 27}}};
  const auto selected = doc["quickActionsTrigger"];
  if (!selected.is<uint8_t>() || selected.as<uint8_t>() != wanted || wanted == 0 || wanted > bindings.size())
    return false;
  unsigned owners = 0;
  for (size_t i = 0; i < bindings.size(); ++i) {
    const auto value = doc[bindings[i].key];
    if (!value.isUnbound() && !value.is<uint8_t>()) return false;
    if (value.is<uint8_t>() && value.as<uint8_t>() == bindings[i].value) {
      if (i + 1 != wanted) return false;
      ++owners;
    }
  }
  return owners == 1;
}
}  // namespace readershortcuts
