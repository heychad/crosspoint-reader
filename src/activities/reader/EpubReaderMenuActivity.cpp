#include "EpubReaderMenuActivity.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "components/UITheme.h"
#include "util/PluginEvents.h"
#include "util/SlideSwitch.h"

namespace fui = freeink::ui;

EpubReaderMenuActivity::EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                               const std::string& title, const int currentPage, const int totalPages,
                                               const int bookProgressPercent, const uint8_t currentOrientation,
                                               const bool hasFootnotes, const bool hasBookmarks,
                                               const bool hasClippings, MenuAction& selectedAction,
                                               const bool completionKnown, const bool completed,
                                               const bool alreadyInReadFolder)
    : UiTabListActivity("EpubReaderMenu", renderer, mappedInput),
      tabbed(SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TABBED),
      selectedAction(selectedAction),
      title(title),
      pendingOrientation(currentOrientation),
      currentPage(currentPage),
      totalPages(totalPages),
      bookProgressPercent(bookProgressPercent) {
  buildMenuItems(menuItems, hasFootnotes, hasBookmarks, hasClippings, completionKnown, completed, alreadyInReadFolder,
                 tabbed);
  if (tabbed) tab = tabForAction(selectedAction);
  rebuildVisibleItems();
}

void EpubReaderMenuActivity::onEnter() {
  RenderLock lock;
  if (tabbed)
    UiTabListActivity::onEnter();
  else
    UiListActivity::onEnter();
  for (int i = 0; i < visibleCount; ++i) {
    if (visibleAction(i) == selectedAction) {
      activeNav().reset(i + (tabbed ? 1 : 0));
      break;
    }
  }
}

void EpubReaderMenuActivity::onExit() {
  const int row = activeNav().selected - (tabbed ? 1 : 0);
  if (row >= 0 && row < visibleCount) selectedAction = visibleAction(row);
  UiListActivity::onExit();
}

EpubReaderMenuActivity::Tab EpubReaderMenuActivity::tabForAction(MenuAction action) {
  switch (action) {
    case MenuAction::TEXT_SETTINGS:
    case MenuAction::FONT_SIZE:
      return Tab::Font;
    case MenuAction::LAYOUT_SETTINGS:
    case MenuAction::STYLE_SETTINGS:
    case MenuAction::ROTATE_SCREEN:
    case MenuAction::RENDER_MODE:
      return Tab::Layout;
    case MenuAction::SELECT_CHAPTER:
    case MenuAction::FOOTNOTES:
    case MenuAction::GO_TO_PERCENT:
    case MenuAction::AUTO_PAGE_TURN:
    case MenuAction::DICTIONARY:
      return Tab::More;
    case MenuAction::BOOKMARKS:
    case MenuAction::TOGGLE_BOOKMARK:
    case MenuAction::SAVE_CLIPPING:
    case MenuAction::VIEW_CLIPPINGS:
    case MenuAction::SAVED_ITEMS:
    case MenuAction::SCREENSHOT:
    case MenuAction::DISPLAY_QR:
      return Tab::Location;
    case MenuAction::NIGHT_MODE:
    case MenuAction::FRONTLIGHT:
    case MenuAction::BLUETOOTH:
    case MenuAction::GO_HOME:
    case MenuAction::SYNC:
    case MenuAction::DELETE_CACHE:
    case MenuAction::READING_STATISTICS:
    case MenuAction::READER_BARS:
    case MenuAction::LIBRARY:
    case MenuAction::FILE_BROWSER:
    case MenuAction::FILE_TRANSFER:
    case MenuAction::HOME_BUTTON:
    case MenuAction::CONTROLS:
    case MenuAction::QUICK_ACTIONS:
    case MenuAction::TOGGLE_COMPLETION:
    case MenuAction::RESET_BOOK_SETTINGS:
      return Tab::Settings;
  }
  return Tab::Settings;
}

const char* EpubReaderMenuActivity::tabLabel(int index) const {
  static constexpr StrId labels[] = {StrId::STR_FONT, StrId::STR_LAYOUT, StrId::STR_TOOL_MORE,
                                     StrId::STR_READER_LOCATION, StrId::STR_SETTINGS_TITLE};
  return I18N.get(labels[index]);
}

void EpubReaderMenuActivity::rebuildVisibleItems() {
  visibleCount = 0;
  for (size_t i = 0; i < menuItems.size() && visibleCount < static_cast<int>(MAX_MENU_ITEMS); ++i) {
    if (!tabbed || tabForAction(menuItems[i].action) == tab) visibleIndexes[visibleCount++] = static_cast<uint8_t>(i);
  }
  buildMenuRowItems();
}

void EpubReaderMenuActivity::onTabAction(int index) {
  if (!tabbed || optionPopup.isActive() || index < 0 || index >= tabCount()) return;
  RenderLock lock(*this);
  app.clearTapFlash();
  if (index == activeTab()) return;
  tab = static_cast<Tab>(index);
  rebuildVisibleItems();
  activeNav().followOnBuild = true;
  requestUpdate();
}

void EpubReaderMenuActivity::stepTab(int direction) {
  if (tabbed) onTabAction((activeTab() + (direction > 0 ? 1 : tabCount() - 1)) % tabCount());
}

fui::ListNav& EpubReaderMenuActivity::activeNav() {
  return tabbed ? UiTabListActivity::activeNav() : UiListActivity::activeNav();
}
void EpubReaderMenuActivity::onRowAction(const fui::ActionEvent& event) {
  if (tabbed)
    UiTabListActivity::onRowAction(event);
  else
    UiListActivity::onRowAction(event);
}
void EpubReaderMenuActivity::navigateButtons() {
  if (tabbed)
    UiTabListActivity::navigateButtons();
  else
    UiListActivity::navigateButtons();
}

// Rebuild labels and row indices at construction and tab changes.
// Repaints refresh only the live row values.
void EpubReaderMenuActivity::buildMenuRowItems() {
  for (int i = 0; i < visibleCount; ++i) {
    fui::ListItem item;
    item.label = I18N.get(menuItems[visibleIndexes[i]].labelId);
    item.actionValue = static_cast<int16_t>(i);
    menuRowItems[i] = item;
  }
}

void EpubReaderMenuActivity::buildMenuItems(std::vector<MenuItem>& items, const bool hasFootnotes,
                                            const bool hasBookmarks, const bool hasClippings,
                                            const bool completionKnown, const bool completed,
                                            const bool alreadyInReadFolder, const bool tabbed) {
  items.clear();
  items.reserve(MAX_MENU_ITEMS);
  items.push_back({MenuAction::SELECT_CHAPTER, StrId::STR_SELECT_CHAPTER});
  if (hasFootnotes) {
    items.push_back({MenuAction::FOOTNOTES, StrId::STR_FOOTNOTES});
  }
  if (hasBookmarks) {
    items.push_back({MenuAction::BOOKMARKS, StrId::STR_BOOKMARKS});
  }
  items.push_back({MenuAction::TOGGLE_BOOKMARK, StrId::STR_TOGGLE_BOOKMARK});
  items.push_back({MenuAction::SAVE_CLIPPING, StrId::STR_SAVE_CLIPPING});
  if (hasClippings) {
    items.push_back({MenuAction::VIEW_CLIPPINGS, StrId::STR_VIEW_CLIPPINGS});
  }
  if (!slideSwitchOwns(CrossPointSettings::SLIDE_SWITCH_NIGHT_MODE)) {
    items.push_back({MenuAction::NIGHT_MODE, StrId::STR_NIGHT_MODE});
  }
  if (Frontlight.present() && !slideSwitchOwnsLight()) {
    items.push_back({MenuAction::FRONTLIGHT, StrId::STR_FRONTLIGHT});
  }
  if (bleturner::status().compiledIn && SETTINGS.ble.peerAddr[0] != '\0') {
    items.push_back({MenuAction::BLUETOOTH, StrId::STR_BT_PAGE_TURNER});
  }
  if (SETTINGS.dictionaryName[0] != '\0') {
    items.push_back({MenuAction::DICTIONARY, StrId::STR_LOOKUP});
  }
  items.push_back({MenuAction::ROTATE_SCREEN, StrId::STR_ORIENTATION});
  items.push_back({MenuAction::AUTO_PAGE_TURN, StrId::STR_AUTO_TURN_PAGES_PER_MIN});
  items.push_back({MenuAction::GO_TO_PERCENT, StrId::STR_GO_TO_PERCENT});
  items.push_back({MenuAction::READING_STATISTICS, StrId::STR_READING_STATISTICS});
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
  (void)alreadyInReadFolder;
  if (completionKnown) {
#else
  if (completionKnown && (!SETTINGS.moveFinishedToReadFolder || completed || alreadyInReadFolder)) {
#endif
    items.push_back({MenuAction::TOGGLE_COMPLETION, completed ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  }
#else
  (void)completionKnown;
  (void)completed;
  (void)alreadyInReadFolder;
#endif
  items.push_back({MenuAction::READER_BARS, StrId::STR_READER_BARS});
  items.push_back({MenuAction::CONTROLS, StrId::STR_CAT_CONTROLS});
  items.push_back({MenuAction::QUICK_ACTIONS, StrId::STR_QUICK_ACTIONS});
  if (BoardConfig::hasHomeKey()) items.push_back({MenuAction::HOME_BUTTON, StrId::STR_HOME_BUTTON});
  items.push_back({MenuAction::SAVED_ITEMS, StrId::STR_SAVED_ITEMS});
  items.push_back({MenuAction::LIBRARY, StrId::STR_LIBRARY});
  items.push_back({MenuAction::FILE_BROWSER, StrId::STR_BROWSE_FILES});
  items.push_back({MenuAction::FILE_TRANSFER, StrId::STR_FILE_TRANSFER});
  items.push_back({MenuAction::SCREENSHOT, StrId::STR_SCREENSHOT_BUTTON});
  items.push_back({MenuAction::DISPLAY_QR, StrId::STR_DISPLAY_QR});
  items.push_back({MenuAction::GO_HOME, StrId::STR_GO_HOME_BUTTON});
  bool canSync = KOREADER_STORE.hasCredentials();
  for (size_t event = 0; !canSync && event < static_cast<size_t>(pluginevents::Event::COUNT); ++event) {
    canSync = pluginevents::anySubscriber(static_cast<pluginevents::Event>(event));
  }
  if (canSync) {
    items.push_back({MenuAction::SYNC, StrId::STR_SYNC_PROGRESS});
  }
  items.push_back({MenuAction::DELETE_CACHE, StrId::STR_DELETE_CACHE});
  items.push_back({MenuAction::TEXT_SETTINGS, tabbed ? StrId::STR_FONT_FAMILY : StrId::STR_TEXT_SETTINGS});
  if (tabbed) {
    items.push_back({MenuAction::FONT_SIZE, StrId::STR_FONT_SIZE});
    items.push_back({MenuAction::LAYOUT_SETTINGS, StrId::STR_LAYOUT});
    items.push_back({MenuAction::STYLE_SETTINGS, StrId::STR_STYLE});
  }
  items.push_back({MenuAction::RENDER_MODE, StrId::STR_EPUB_RENDER_MODE});
  items.push_back({MenuAction::RESET_BOOK_SETTINGS, StrId::STR_RESET_BOOK_SETTINGS});
}

void EpubReaderMenuActivity::toggleBluetooth() {
  SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
  if (!SETTINGS.ble.enabled) bleturner::switchOff();
  SETTINGS.saveToFile();
  // Enabling waits for the main-loop lifecycle's reader heap gates, then says in the status bar
  // whether a remote linked.
  if (SETTINGS.ble.enabled) bleturner::requestConnect();
}

void EpubReaderMenuActivity::closeCancelled() {
  ActivityResult result;
  result.isCancelled = true;
  result.data = MenuResult{-1, pendingOrientation, selectedPageTurnOption};
  setResult(std::move(result));
  finish();
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= visibleCount) return;
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  activeNav().selected = index + (tabbed ? 1 : 0);

  const auto selectedAction = visibleAction(index);
  if (selectedAction == MenuAction::ROTATE_SCREEN) {
    optionPopup.show(StrId::STR_ORIENTATION, orientationLabels.data(), static_cast<int>(orientationLabels.size()),
                     pendingOrientation, [this](int idx) {
                       pendingOrientation = idx;
                       // Rotate the menu immediately. Only the renderer turns;
                       // SETTINGS.orientation stays unchanged so the reader's
                       // result handler still detects the change and reflows.
                       ReaderUtils::applyOrientation(renderer, pendingOrientation);
                       app.setDevice(uiTarget.deviceContext());  // hit rects follow the new frame
                       requestUpdate(true);
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::AUTO_PAGE_TURN) {
    optionPopup.show(I18N.get(StrId::STR_AUTO_TURN_PAGES_PER_MIN), pageTurnLabels.data(),
                     static_cast<int>(pageTurnLabels.size()), selectedPageTurnOption, [this](int idx) {
                       selectedPageTurnOption = idx;
                       requestUpdate();
                     });
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::NIGHT_MODE) {
    SETTINGS.screenInverted = SETTINGS.screenInverted == 0 ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::BLUETOOTH) {
    toggleBluetooth();
    requestUpdate();
    return;
  }

  if (selectedAction == MenuAction::FRONTLIGHT) {
    const bool lightOn = !Frontlight.isOn();
    Frontlight.setOn(lightOn);
    SETTINGS.frontlightOn = lightOn ? 1 : 0;
    SETTINGS.saveToFile();
    requestUpdate();
    return;
  }

  setResult(MenuResult{static_cast<int>(selectedAction), pendingOrientation, selectedPageTurnOption});
  finish();
}

bool EpubReaderMenuActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    closeCancelled();
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (tabbed && ringPos() == 0)
      stepTab(1);
    else
      activateIndex(activeNav().selected - (tabbed ? 1 : 0));
    return true;
  }

  return false;
}

void EpubReaderMenuActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});

  if (tabbed) buildTabBar(screen);

  // Progress summary where the old sub-header band sat.
  std::string progressLine;
  if (totalPages > 0) {
    progressLine = std::string(tr(STR_CHAPTER_PREFIX)) + std::to_string(currentPage) + "/" +
                   std::to_string(totalPages) + std::string(tr(STR_PAGES_SEPARATOR));
  }
  progressLine += std::string(tr(STR_BOOK_PREFIX)) + std::to_string(bookProgressPercent) + "%";
  const fui::Rect band = screen.takeTop(static_cast<int16_t>(metrics.tabBarHeight));
  const int16_t pad = screen.theme().headerSidePadding;
  screen.target().text(band.inset(fui::Insets{0, pad, 0, pad}), progressLine.c_str(), screen.theme().smallText);
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  for (int i = 0; i < visibleCount; ++i) {
    const auto action = visibleAction(i);
    if (action == MenuAction::ROTATE_SCREEN) {
      menuRowItems[i].value = I18N.get(orientationLabels[pendingOrientation]);
    } else if (action == MenuAction::AUTO_PAGE_TURN) {
      menuRowItems[i].value = pageTurnLabels[selectedPageTurnOption];
    } else if (action == MenuAction::RENDER_MODE) {
      static constexpr StrId labels[] = {StrId::STR_RENDER_DEFAULT, StrId::STR_RENDER_BALANCED,
                                         StrId::STR_RENDER_LIGHT};
      menuRowItems[i].value = I18N.get(labels[static_cast<uint8_t>(normalizeEpubRenderMode(SETTINGS.epubRenderMode))]);
    } else if (action == MenuAction::NIGHT_MODE) {
      GUI.setCheckboxRow(menuRowItems[i], SETTINGS.screenInverted);
    } else if (action == MenuAction::BLUETOOTH) {
      GUI.setCheckboxRow(menuRowItems[i], SETTINGS.ble.enabled);
    } else if (action == MenuAction::FRONTLIGHT) {
      GUI.setCheckboxRow(menuRowItems[i], Frontlight.isOn());
    }
  }

  fui::ListProps props;
  props.items = menuRowItems;
  props.count = static_cast<uint16_t>(visibleCount);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  if (tabbed)
    syncTabListViewport(screen, props);
  else
    syncListViewport(screen, props);
  screen.list(props);
}

void EpubReaderMenuActivity::drawChrome() {
  // Header via GUI.drawHeader (already FreeInkUI-themed) for the battery
  // indicator; the rest of the screen renders through the app.
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), title.c_str());
}

void EpubReaderMenuActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  UiListActivity::render(std::move(lock));
}
