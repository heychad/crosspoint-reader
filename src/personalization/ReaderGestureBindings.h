#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

#include "ReaderShortcuts.h"

namespace readergestures {
// Values match CrossInk's persisted twoFingerSwipe* action fields.
enum class Binding : uint8_t {
  None,
  Brighter,
  Dimmer,
  Warmer,
  Cooler,
  NextChapter,
  PreviousChapter,
  LargerText,
  SmallerText,
  Home,
  HomeReader,
  SelectChapter,
  Count
};
inline constexpr std::array<const char*, 4> KEYS = {"twoFingerSwipeUp", "twoFingerSwipeDown", "twoFingerSwipeLeft",
                                                    "twoFingerSwipeRight"};

inline readershortcuts::Action actionFor(uint8_t value) {
  using A = readershortcuts::Action;
  static constexpr std::array actions = {A::Default,     A::Brighter,    A::Dimmer,          A::Warmer,
                                         A::Cooler,      A::NextChapter, A::PreviousChapter, A::LargerText,
                                         A::SmallerText, A::Home,        A::HomeReader,      A::SelectChapter};
  return value < actions.size() ? actions[value] : A::Default;
}

inline bool normalize(std::array<uint8_t, 4>& values, int edited = -1) {
  const auto before = values;
  std::replace_if(
      values.begin(), values.end(), [](uint8_t value) { return value >= static_cast<uint8_t>(Binding::Count); },
      uint8_t{0});
  if (edited >= 0 && edited < static_cast<int>(values.size()) && values[edited]) {
    for (size_t i = 0; i < values.size(); ++i)
      if (static_cast<int>(i) != edited && values[i] == values[edited]) values[i] = 0;
  }
  for (size_t i = 0; i < values.size(); ++i) {
    if (!values[i]) continue;
    for (size_t j = i + 1; j < values.size(); ++j)
      if (values[j] == values[i]) values[j] = 0;
  }
  return values != before;
}
}  // namespace readergestures
