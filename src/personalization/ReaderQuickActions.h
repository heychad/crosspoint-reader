#pragma once
#include <ArduinoJson.h>

#include <array>

#include "ReaderShortcuts.h"

namespace readerquickactions {
using Action = readershortcuts::Action;
inline constexpr size_t SLOT_COUNT = 5;
using Slots = std::array<uint8_t, SLOT_COUNT>;
inline bool isSlotAction(uint8_t value) {
  return value > static_cast<uint8_t>(Action::Disabled) && value < readershortcuts::GENERAL_ACTION_COUNT &&
         value != static_cast<uint8_t>(Action::QuickActions) && value != static_cast<uint8_t>(Action::QuickLock);
}
inline void write(JsonDocument& doc, const Slots& slots) {
  auto values = doc["readerQuickActionsV1"].to<JsonArray>();
  for (auto value : slots) values.add(value);
}
inline bool read(JsonVariantConst doc, Slots& slots) {
  const auto source = doc["readerQuickActionsV1"];
  if (source.isNull()) return false;
  if (!source.is<JsonArrayConst>()) return true;
  const auto values = source.as<JsonArrayConst>();
  bool corrected = values.size() != slots.size();
  for (size_t i = 0; i < slots.size(); ++i) {
    const auto value = values[i];
    if (value.isNull()) {
      slots[i] = 0;
      continue;
    }
    if (!value.is<uint8_t>() || (value.as<uint8_t>() != 0 && !isSlotAction(value.as<uint8_t>()))) {
      slots[i] = 0;
      corrected = true;
    } else
      slots[i] = value.as<uint8_t>();
  }
  return corrected;
}
template <size_t N>
bool normalizeTriggers(std::array<uint8_t, N>& values, int preferred = -1) {
  int owner = -1;
  const auto action = static_cast<uint8_t>(Action::QuickActions);
  for (size_t i = 0; i < N; ++i)
    if (values[i] == action && (owner < 0 || static_cast<int>(i) == preferred)) owner = static_cast<int>(i);
  bool changed = false;
  for (size_t i = 0; i < N; ++i)
    if (values[i] == action && static_cast<int>(i) != owner) {
      values[i] = 0;
      changed = true;
    }
  return changed;
}

}  // namespace readerquickactions
