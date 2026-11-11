#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>

#include "LegacyQuickOwner.h"
#include "ReaderQuickActions.h"
#include "ReaderShortcuts.h"

namespace readershortcuts {
struct LegacySideMapping {
  uint8_t legacy;
  Action action;
};

inline bool readLegacySideAction(uint8_t value, bool chord, Action& result) {
  static constexpr std::array<LegacySideMapping, 33> single = {{LegacySideMapping{0, Action::Disabled},
                                                                {1, Action::Sleep},
                                                                {2, Action::NextPage},
                                                                {3, Action::Refresh},
                                                                {4, Action::CycleFontFamily},
                                                                {6, Action::ToggleFocus},
                                                                {7, Action::Bookmark},
                                                                {8, Action::SyncProgress},
                                                                {9, Action::ToggleCompletion},
                                                                {10, Action::ReadingStatistics},
                                                                {11, Action::Screenshot},
                                                                {13, Action::FileTransfer},
                                                                {14, Action::ToggleTilt},
                                                                {15, Action::ToggleNight},
                                                                {16, Action::Footnotes},
                                                                {17, Action::FileBrowser},
                                                                {21, Action::CreateClipping},
                                                                {22, Action::Dictionary},
                                                                {26, Action::ToggleHomeButton},
                                                                {28, Action::ToggleFrontlight},
                                                                {29, Action::ToggleTouch},
                                                                {30, Action::QuickLock},
                                                                {31, Action::PreviousPage},
                                                                {33, Action::Library},
                                                                {36, Action::HomeReader},
                                                                {38, Action::SelectChapter},
                                                                {64, Action::PreviousChapter},
                                                                {65, Action::NextChapter},
                                                                {66, Action::LargerText},
                                                                {67, Action::SmallerText},
                                                                {68, Action::RotateBackward},
                                                                {69, Action::Rotate},
                                                                {70, Action::RotateHalfTurn}}};
  static constexpr std::array<LegacySideMapping, 24> chords = {{LegacySideMapping{0, Action::Screenshot},
                                                                {1, Action::QuickLock},
                                                                {4, Action::Disabled},
                                                                {5, Action::Sleep},
                                                                {6, Action::NextPage},
                                                                {7, Action::Bookmark},
                                                                {8, Action::ReadingStatistics},
                                                                {9, Action::ToggleCompletion},
                                                                {10, Action::Refresh},
                                                                {11, Action::CycleFontFamily},
                                                                {13, Action::ToggleFocus},
                                                                {15, Action::SyncProgress},
                                                                {16, Action::FileTransfer},
                                                                {20, Action::ToggleNight},
                                                                {21, Action::Footnotes},
                                                                {22, Action::FileBrowser},
                                                                {23, Action::CreateClipping},
                                                                {24, Action::Dictionary},
                                                                {25, Action::ToggleHomeButton},
                                                                {27, Action::ToggleFrontlight},
                                                                {28, Action::ToggleTouch},
                                                                {29, Action::PreviousPage},
                                                                {31, Action::Library},
                                                                {32, Action::HomeReader}}};
  const auto find = [&](const auto& mappings) {
    for (const auto& mapping : mappings) {
      if (mapping.legacy == value) {
        result = mapping.action;
        return true;
      }
    }
    return false;
  };
  return chord ? find(chords) : find(single);
}

inline bool legacySideOrientation(JsonVariantConst doc, uint8_t& orientation) {
  const auto legacy = doc["sideButtonOrientationAware"];
  if (!legacy.is<uint8_t>() || legacy.as<uint8_t>() > 1) return false;
  const auto current = doc["readerSideFollowOrientation"];
  if (!current.isUnbound() && (!current.is<uint8_t>() || current.as<uint8_t>() != legacy.as<uint8_t>())) return false;
  if (current.isUnbound() && legacy.as<uint8_t>() == 1) {
    for (const char* key : {"readerUpTap", "readerUpHold", "readerDownTap", "readerDownHold", "readerSideChord"})
      if (!doc[key].isUnbound()) return false;
  }
  orientation = legacy.as<uint8_t>();
  return true;
}

inline bool readEmbeddedLegacySideOrientation(JsonVariantConst doc, uint8_t& destination) {
  if (!doc["readerSideFollowOrientation"].isUnbound()) return false;
  uint8_t orientation;
  if (!legacySideOrientation(doc, orientation)) return false;
  destination = orientation;
  return true;
}

// Quick Actions is handled separately because its trigger must have one owner.
inline bool readEmbeddedLegacySides(JsonVariantConst doc, const std::array<uint8_t*, 5>& destinations) {
  uint8_t orientation;
  if (!legacySideOrientation(doc, orientation)) return false;
  static constexpr std::array<const char*, 5> legacy = {"sideButtonUpShort", "sideButtonUpLong", "sideButtonDownShort",
                                                        "sideButtonDownLong", "sideButtonChordAction"};
  static constexpr std::array<const char*, 5> current = {"readerUpTap", "readerUpHold", "readerDownTap",
                                                         "readerDownHold", "readerSideChord"};
  bool imported = false;
  for (size_t i = 0; i < legacy.size(); ++i) {
    if (!doc[current[i]].isUnbound() || !doc[legacy[i]].is<uint8_t>()) continue;
    Action action;
    if (!readLegacySideAction(doc[legacy[i]].as<uint8_t>(), i == 4, action)) continue;
    *destinations[i] = static_cast<uint8_t>(action);
    imported = true;
  }
  return imported;
}

inline bool readEmbeddedLegacyQuickSlots(JsonVariantConst doc, readerquickactions::Slots& slots) {
  if (!doc["readerQuickActionsV1"].isUnbound()) return false;
  const auto source = doc["quickActionSlots"].as<JsonArrayConst>();
  if (source.isNull() || source.size() != slots.size()) return false;
  readerquickactions::Slots converted{};
  for (size_t i = 0; i < converted.size(); ++i) {
    if (!source[i].is<uint8_t>()) return false;
    const uint8_t raw = source[i].as<uint8_t>();
    if (raw == 0) continue;
    Action action;
    if (raw >= 64 || !readLegacySideAction(raw, false, action) ||
        !readerquickactions::isSlotAction(static_cast<uint8_t>(action)))
      return false;
    converted[i] = static_cast<uint8_t>(action);
  }
  slots = converted;
  return true;
}

inline bool readEmbeddedLegacySideQuickAction(JsonVariantConst doc, const std::array<uint8_t*, 5>& destinations,
                                              bool hasCurrentTrigger) {
  uint8_t orientation;
  if (hasCurrentTrigger || !legacySideOrientation(doc, orientation) || !doc["quickActionsTrigger"].is<uint8_t>())
    return false;
  const uint8_t trigger = doc["quickActionsTrigger"].as<uint8_t>();
  // CrossInk trigger 9 is Up+Down; 10-13 are the four side bindings.
  if (trigger < 9 || trigger > 13) return false;
  const size_t selected = trigger == 9 ? 4 : trigger - 10;
  static constexpr std::array<const char*, 5> current = {"readerUpTap", "readerUpHold", "readerDownTap",
                                                         "readerDownHold", "readerSideChord"};
  if (!doc[current[selected]].isUnbound()) return false;
  if (!uniqueLegacyQuickOwner(doc, trigger)) return false;
  *destinations[selected] = static_cast<uint8_t>(Action::QuickActions);
  return true;
}
}  // namespace readershortcuts
