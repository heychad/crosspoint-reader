#pragma once

#include "ReaderShortcuts.h"
#include "util/HomeButtonInput.h"

namespace homereadershortcuts {
inline readershortcuts::Action actionFor(HomeButtonAction action) {
  using Action = readershortcuts::Action;
  switch (action) {
    case HomeButtonAction::FileBrowser:
      return Action::FileBrowser;
    case HomeButtonAction::Library:
      return Action::Library;
    case HomeButtonAction::SavedItems:
      return Action::SavedItems;
    case HomeButtonAction::FileTransfer:
      return Action::FileTransfer;
    case HomeButtonAction::ReadingStatistics:
      return Action::ReadingStatistics;
    default:
      // Existing Home actions retain their current owners.
      return Action::Default;
  }
}
}  // namespace homereadershortcuts
