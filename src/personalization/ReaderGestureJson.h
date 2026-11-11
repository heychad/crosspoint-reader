#pragma once
#include <ArduinoJson.h>

#include "ReaderGestureBindings.h"

namespace readergestures {
// Import only fields embedded in this already-parsed settings document. A
// modern key, including an explicit null, always takes precedence.
inline bool readEmbeddedLegacyEdges(JsonVariantConst doc, const std::array<uint8_t*, 4>& destinations) {
  static constexpr std::array<const char*, 4> legacy = {"leftEdgeUp", "leftEdgeDown", "rightEdgeUp", "rightEdgeDown"};
  static constexpr std::array<const char*, 4> current = {"readerLeftEdgeUp", "readerLeftEdgeDown", "readerRightEdgeUp",
                                                         "readerRightEdgeDown"};
  bool imported = false;
  for (size_t i = 0; i < legacy.size(); ++i) {
    if (!doc[current[i]].isUnbound()) continue;
    const auto value = doc[legacy[i]];
    if (!value.is<uint8_t>()) continue;
    const uint8_t raw = value.as<uint8_t>();
    if (raw >= static_cast<uint8_t>(Binding::Count)) continue;
    const auto mapped = static_cast<uint8_t>(actionFor(raw));
    if (mapped >= readershortcuts::EDGE_ACTION_COUNT) continue;
    *destinations[i] = mapped;
    imported = true;
  }
  return imported;
}

inline void writeBindings(JsonDocument& doc, const std::array<uint8_t, 4>& values) {
  for (size_t i = 0; i < values.size(); ++i) doc[KEYS[i]] = values[i];
}
inline bool readBindings(JsonVariantConst doc, std::array<uint8_t, 4>& values) {
  bool corrected = false;
  for (size_t i = 0; i < values.size(); ++i) {
    const auto value = doc[KEYS[i]];
    if (value.isNull()) continue;
    if (!value.is<uint8_t>() || value.as<uint8_t>() >= static_cast<uint8_t>(Binding::Count)) {
      values[i] = 0;
      corrected = true;
    } else
      values[i] = value.as<uint8_t>();
  }
  return normalize(values) || corrected;
}
}  // namespace readergestures
