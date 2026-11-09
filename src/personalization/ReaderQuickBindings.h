#pragma once
#include "CrossPointSettings.h"
#include "ReaderQuickActions.h"

namespace readerquickactions {
struct Binding {
  uint8_t CrossPointSettings::* field;
  uint8_t active;
  uint8_t fallback;
};
inline constexpr uint8_t QUICK = static_cast<uint8_t>(Action::QuickActions);
inline constexpr std::array<Binding, 19> BINDINGS = {
    {{&CrossPointSettings::powerTapAction, QUICK, 0},
     {&CrossPointSettings::powerHoldAction, QUICK, 0},
     {&CrossPointSettings::powerUpChordAction, QUICK, 0},
     {&CrossPointSettings::backHoldAction, QUICK, 0},
     {&CrossPointSettings::confirmHoldAction, QUICK, 0},
     {&CrossPointSettings::readerUpTap, QUICK, 0},
     {&CrossPointSettings::readerUpHold, QUICK, 0},
     {&CrossPointSettings::readerDownTap, QUICK, 0},
     {&CrossPointSettings::readerDownHold, QUICK, 0},
     {&CrossPointSettings::readerSideChord, QUICK, 0},
     {&CrossPointSettings::readerLeftEdgeUp, QUICK, 0},
     {&CrossPointSettings::readerLeftEdgeDown, QUICK, 0},
     {&CrossPointSettings::readerRightEdgeUp, QUICK, 0},
     {&CrossPointSettings::readerRightEdgeDown, QUICK, 0},
     {&CrossPointSettings::homeButtonTapAction, static_cast<uint8_t>(HomeButtonAction::QuickActions),
      static_cast<uint8_t>(HomeButtonAction::Home)},
     {&CrossPointSettings::homeButtonDoubleTapAction, static_cast<uint8_t>(HomeButtonAction::QuickActions),
      static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight)},
     {&CrossPointSettings::homeButtonLongPressAction, static_cast<uint8_t>(HomeButtonAction::QuickActions),
      static_cast<uint8_t>(HomeButtonAction::ReaderMenu)},
     {&CrossPointSettings::shortPwrBtn, CrossPointSettings::PWR_QUICK_ACTIONS, CrossPointSettings::IGNORE},
     {&CrossPointSettings::longPressMenuFunction, CrossPointSettings::LP_MENU_QUICK_ACTIONS,
      CrossPointSettings::LP_MENU_DISABLED}}};
inline auto bindingValues(const CrossPointSettings& settings) {
  std::array<uint8_t, BINDINGS.size()> values{};
  for (size_t i = 0; i < BINDINGS.size(); ++i)
    values[i] = settings.*BINDINGS[i].field == BINDINGS[i].active ? QUICK : 0;
  return values;
}
inline bool normalizeBindings(CrossPointSettings& settings, uint8_t CrossPointSettings::* preferred = nullptr) {
  auto values = bindingValues(settings);
  int preferredIndex = -1;
  for (size_t i = 0; i < BINDINGS.size(); ++i)
    if (BINDINGS[i].field == preferred) preferredIndex = static_cast<int>(i);
  if (!normalizeTriggers(values, preferredIndex)) return false;
  for (size_t i = 0; i < BINDINGS.size(); ++i) {
    const auto& binding = BINDINGS[i];
    if (settings.*binding.field == binding.active && values[i] != QUICK) settings.*binding.field = binding.fallback;
  }
  return true;
}
inline bool normalizeChangedBindings(CrossPointSettings& settings, const std::array<uint8_t, BINDINGS.size()>& before) {
  const auto after = bindingValues(settings);
  uint8_t CrossPointSettings::* preferred = nullptr;
  for (size_t i = 0; i < BINDINGS.size(); ++i) {
    if (after[i] != before[i] && after[i] == QUICK) {
      preferred = BINDINGS[i].field;
      break;
    }
  }
  return normalizeBindings(settings, preferred);
}
}  // namespace readerquickactions
