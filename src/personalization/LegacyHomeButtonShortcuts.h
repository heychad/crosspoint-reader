#pragma once

#include <array>
#include <cstdint>

#include "util/HomeButtonInput.h"

namespace readershortcuts {
// CrossInk and the fork reuse the Home-key field names with different IDs.
inline bool readLegacyHomeAction(uint8_t raw, HomeButtonAction& action) {
  struct Mapping {
    uint8_t raw;
    HomeButtonAction action;
  };
  static constexpr std::array<Mapping, 25> mappings = {{{0, HomeButtonAction::Ignore},
                                                        {2, HomeButtonAction::NextPage},
                                                        {3, HomeButtonAction::Refresh},
                                                        {4, HomeButtonAction::CycleFontFamily},
                                                        {7, HomeButtonAction::Bookmark},
                                                        {8, HomeButtonAction::Sync},
                                                        {9, HomeButtonAction::ToggleCompletion},
                                                        {10, HomeButtonAction::ReadingStatistics},
                                                        {13, HomeButtonAction::FileTransfer},
                                                        {16, HomeButtonAction::Footnotes},
                                                        {17, HomeButtonAction::FileBrowser},
                                                        {21, HomeButtonAction::CreateClipping},
                                                        {22, HomeButtonAction::Dictionary},
                                                        {23, HomeButtonAction::Home},
                                                        {24, HomeButtonAction::ToggleFrontlight},
                                                        {25, HomeButtonAction::ReaderMenu},
                                                        {30, HomeButtonAction::QuickLock},
                                                        {1, HomeButtonAction::Sleep},
                                                        {11, HomeButtonAction::Screenshot},
                                                        {14, HomeButtonAction::ToggleTilt},
                                                        {15, HomeButtonAction::ToggleNight},
                                                        {28, HomeButtonAction::ToggleFrontlight},
                                                        {29, HomeButtonAction::ToggleTouch},
                                                        {31, HomeButtonAction::PreviousPage},
                                                        {34, HomeButtonAction::Sleep}}};
  if (raw == 33) {
    action = HomeButtonAction::Library;
    return true;
  }
  for (const auto& mapping : mappings) {
    if (mapping.raw == raw) {
      action = mapping.action;
      return true;
    }
  }
  return false;
}
}  // namespace readershortcuts
