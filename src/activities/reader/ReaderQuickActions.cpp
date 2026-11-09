#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include <Memory.h>

#include "QuickActionsActivity.h"
#include "ReaderActivity.h"
#include "personalization/HomeReaderShortcuts.h"

using readershortcuts::Action;

bool ReaderActivity::handleHomeReaderShortcut() {
  const Action action = homereadershortcuts::actionFor(mappedInput.homeButtonAction());
  if (action == Action::Default) return false;
  {
    RenderLock lock(*this);
    if (!pageRendered.load(std::memory_order_acquire) || inputOverPage()) return true;
  }
  if (supportsReaderShortcut(action)) handleReaderShortcut(action);
  return true;
}

bool ReaderActivity::supportsReaderShortcut(Action action) const {
  switch (action) {
    case Action::Sleep:
    case Action::Refresh:
    case Action::Home:
    case Action::FileBrowser:
    case Action::FileTransfer:
    case Action::Library:
    case Action::SavedItems:
    case Action::ReadingStatistics:
    case Action::ToggleNight:
      return true;
    case Action::PreviousPage:
    case Action::NextPage:
    case Action::Screenshot:
      return true;
    case Action::ToggleTouch:
      return mappedInput.hasTouch();
    case Action::ToggleTilt:
      return halTiltSensor.isAvailable();
    case Action::ToggleFrontlight:
    case Action::Brighter:
    case Action::Dimmer:
      return Frontlight.present();
    case Action::Warmer:
    case Action::Cooler:
      return Frontlight.present() && Frontlight.hasColorTemperature();
    default:
      return false;
  }
}

void ReaderActivity::openQuickActions() {
  {
    RenderLock lock(*this);
    if (!pageRendered.load(std::memory_order_acquire) || inputOverPage()) return;
    onReaderGestureStarted();
  }
  uint64_t allowed = 0;
  static_assert(static_cast<unsigned>(Action::Count) < 64);
  for (unsigned i = 0; i < static_cast<unsigned>(Action::Count); ++i) {
    if (supportsReaderShortcut(static_cast<Action>(i))) allowed |= uint64_t{1} << i;
  }
  auto menu = makeUniqueNoThrow<QuickActionsActivity>(renderer, mappedInput, QuickActionsActivity::Mode::Run, allowed);
  if (!menu) {
    LOG_ERR("READER", "OOM: Quick Actions");
    return;
  }
  startActivityForResult(std::move(menu), [this](const ActivityResult& result) {
    const auto* selected = std::get_if<MenuResult>(&result.data);
    if (result.isCancelled || !selected || !readerquickactions::isSlotAction(selected->action)) return;
    RenderLock lock(*this);
    pendingQuickAction = static_cast<Action>(selected->action);
    quickActionPaintGeneration = renderedPageGeneration;
    requestUpdate();
  });
}

bool ReaderActivity::handlePendingQuickAction() {
  if (pendingQuickAction == Action::Default) return false;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    pendingQuickAction = Action::Default;
    return false;
  }
  Action action = Action::Default;
  {
    RenderLock lock(RenderLock::Mode::Try);
    if (!lock.ownsLock() || renderedPageGeneration == quickActionPaintGeneration) return true;
    action = pendingQuickAction;
    pendingQuickAction = Action::Default;
    // The new page has already painted; resume accounting before dispatching
    // actions such as light changes that do not need another repaint.
    readingStats.onRender(isAtEndOfBook() ? ReadingStatsFrame::EndOfBook : ReadingStatsFrame::Page);
  }
  handleReaderShortcut(action);
  return true;
}
