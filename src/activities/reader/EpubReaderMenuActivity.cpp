#include "EpubReaderMenuActivity.h"

#include <BlePageTurner.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>
#include <HalFrontlight.h>
#include <I18n.h>

#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "ReaderLineSpacing.h"
#include "ReaderUtils.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "util/PluginEvents.h"
#include "util/SlideSwitch.h"

namespace fui = freeink::ui;

namespace {
using MA = EpubReaderMenuActivity::MenuAction;
constexpr StrId WORD_SPACING[] = {StrId::STR_SPACING_50_PERCENT,  StrId::STR_SPACING_75_PERCENT,
                                  StrId::STR_SPACING_100_PERCENT, StrId::STR_SPACING_125_PERCENT,
                                  StrId::STR_SPACING_150_PERCENT, StrId::STR_SPACING_175_PERCENT,
                                  StrId::STR_SPACING_200_PERCENT};
constexpr StrId CHARACTER_SPACING[] = {StrId::STR_SPACING_MINUS_2, StrId::STR_SPACING_MINUS_1, StrId::STR_SPACING_ZERO,
                                       StrId::STR_SPACING_PLUS_1, StrId::STR_SPACING_PLUS_2};
constexpr StrId ALIGNMENT[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                               StrId::STR_BOOK_S_STYLE};
constexpr StrId IMAGES[] = {StrId::STR_IMAGES_DISPLAY, StrId::STR_IMAGES_PLACEHOLDER, StrId::STR_IMAGES_SUPPRESS,
                            StrId::STR_IMAGES_DISPLAY_BW};
constexpr const char* MARGINS[] = {"5", "10", "15", "20", "25", "30", "35", "40"};
constexpr const char* INDENTS[] = {"0", "1", "2", "3", "4", "5"};
static_assert(CrossPointSettings::SCREEN_MARGIN_MIN == 5 && CrossPointSettings::SCREEN_MARGIN_MAX == 40 &&
              CrossPointSettings::SCREEN_MARGIN_STEP == 5);
static_assert(CrossPointSettings::WORD_SPACING_MIN == 50 && CrossPointSettings::WORD_SPACING_MAX == 200 &&
              CrossPointSettings::WORD_SPACING_STEP == 25);

struct TextControl {
  MA action;
  StrId title;
  uint8_t CrossPointSettings::* field;
  const StrId* ids = nullptr;
  const char* const* text = nullptr;
  uint8_t count = 0;  // zero is an on/off control
  uint8_t minimum = 0;
  uint8_t step = 1;
};
constexpr TextControl TEXT_CONTROLS[] = {
    {MA::LINE_SPACING, StrId::STR_LINE_SPACING, &CrossPointSettings::lineSpacing, LINE_SPACING_IDS, nullptr, 7},
    {MA::WORD_SPACING, StrId::STR_WORD_SPACING, &CrossPointSettings::wordSpacing, WORD_SPACING, nullptr, 7, 50, 25},
    {MA::CHARACTER_SPACING, StrId::STR_CHARACTER_SPACING, &CrossPointSettings::characterSpacing, CHARACTER_SPACING,
     nullptr, 5},
    {MA::TEXT_AA, StrId::STR_TEXT_AA, &CrossPointSettings::textAntiAliasing},
    {MA::FOCUS_READING, StrId::STR_FOCUS_READING, &CrossPointSettings::focusReadingEnabled},
    {MA::SCREEN_MARGIN, StrId::STR_SCREEN_MARGIN, &CrossPointSettings::screenMargin, nullptr, MARGINS, 8, 5, 5},
    {MA::ALIGNMENT, StrId::STR_ALIGNMENT, &CrossPointSettings::paragraphAlignment, ALIGNMENT, nullptr, 5},
    {MA::HYPHENATION, StrId::STR_HYPHENATION, &CrossPointSettings::hyphenationEnabled},
    {MA::PARAGRAPH_SPACING, StrId::STR_EXTRA_SPACING, &CrossPointSettings::extraParagraphSpacing},
    {MA::PARAGRAPH_INDENT, StrId::STR_PARAGRAPH_INDENTATION, &CrossPointSettings::paragraphIndentSpaces, nullptr,
     INDENTS, 6},
    {MA::EMBEDDED_STYLE, StrId::STR_EMBEDDED_STYLE, &CrossPointSettings::embeddedStyle},
    {MA::IMAGES, StrId::STR_IMAGES, &CrossPointSettings::imageRendering, IMAGES, nullptr, 4}};
const TextControl* textControl(MA action) {
  for (const auto& control : TEXT_CONTROLS)
    if (control.action == action) return &control;
  return nullptr;
}
}  // namespace

EpubReaderMenuActivity::EpubReaderMenuActivity(
    GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title, const int currentPage,
    const int totalPages, const int bookProgressPercent, const uint8_t currentOrientation, const bool hasFootnotes,
    const bool hasBookmarks, const bool hasClippings, MenuAction& selectedAction, const bool completionKnown,
    const bool completed, const bool alreadyInReadFolder, std::function<void()> textSettingsChanged)
    : UiTabListActivity("EpubReaderMenu", renderer, mappedInput),
      tabbed(SETTINGS.readerMenuStyle == CrossPointSettings::READER_MENU_TABBED),
      textSettingsChanged(std::move(textSettingsChanged)),
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
  if (pane == Pane::FontFamily)
    selectedAction = MenuAction::TEXT_SETTINGS;
  else if (pane == Pane::FontSize)
    selectedAction = MenuAction::FONT_SIZE;
  else if (row >= 0 && row < visibleCount)
    selectedAction = visibleAction(row);
  UiListActivity::onExit();
}

EpubReaderMenuActivity::Tab EpubReaderMenuActivity::tabForAction(MenuAction action) {
  switch (action) {
    case MenuAction::TEXT_SETTINGS:
    case MenuAction::FONT_SIZE:
    case MenuAction::LINE_SPACING:
    case MenuAction::WORD_SPACING:
    case MenuAction::CHARACTER_SPACING:
    case MenuAction::TEXT_AA:
    case MenuAction::FOCUS_READING:
      return Tab::Font;
    case MenuAction::LAYOUT_SETTINGS:
    case MenuAction::STYLE_SETTINGS:
    case MenuAction::ROTATE_SCREEN:
    case MenuAction::RENDER_MODE:
    case MenuAction::SCREEN_MARGIN:
    case MenuAction::ALIGNMENT:
    case MenuAction::HYPHENATION:
    case MenuAction::PARAGRAPH_SPACING:
    case MenuAction::PARAGRAPH_INDENT:
    case MenuAction::EMBEDDED_STYLE:
    case MenuAction::IMAGES:
      return Tab::Layout;
    case MenuAction::SELECT_CHAPTER:
    case MenuAction::FOOTNOTES:
    case MenuAction::GO_TO_PERCENT:
    case MenuAction::AUTO_PAGE_TURN:
    case MenuAction::DICTIONARY:
      return Tab::More;
    case MenuAction::BOOKMARKS:
    case MenuAction::TOGGLE_BOOKMARK:
    case MenuAction::DELETE_BOOKMARKS:
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
    case MenuAction::BOOK_DICTIONARY:
    case MenuAction::TRACK_BOOK_STATS:
    case MenuAction::RESET_BOOK_STATS:
    case MenuAction::RESET_READING_PACE:
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
  if (pane != Pane::Root) closeFontPane();
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
  items.push_back({MenuAction::DELETE_BOOKMARKS, StrId::STR_DELETE_BOOKMARKS});
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
  items.push_back({MenuAction::TRACK_BOOK_STATS, StrId::STR_TRACK_READING_STATS});
  items.push_back({MenuAction::BOOK_DICTIONARY, StrId::STR_DICTIONARY});
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
  items.push_back({MenuAction::RESET_BOOK_STATS, StrId::STR_BOOK_STATS_RESET});
  items.push_back({MenuAction::RESET_READING_PACE, StrId::STR_READING_PACE_RESET});
#endif
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
    for (const auto& control : TEXT_CONTROLS) items.push_back({control.action, control.title});
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

int EpubReaderMenuActivity::listCount() const {
  switch (pane) {
    case Pane::Root:
      return visibleCount;
    case Pane::FontFamily:
      return std::min<int>(32767, CrossPointSettings::BUILTIN_FONT_COUNT + sdFontSystem.registry().getFamilyCount());
    case Pane::FontSize:
      return static_cast<int>(fontSizes.size());
  }
  return 0;
}

int EpubReaderMenuActivity::currentFontRow() const {
  if (pane == Pane::FontSize) {
    const auto point = snapToNearestPointSize(fontSizes, SETTINGS.fontPointSize);
    const auto found = std::find(fontSizes.begin(), fontSizes.end(), point);
    return found == fontSizes.end() ? 0 : static_cast<int>(found - fontSizes.begin());
  }
  if (SETTINGS.sdFontFamilyName[0]) {
    const auto& families = sdFontSystem.registry().getFamilies();
    for (size_t i = 0; i < families.size(); ++i)
      if (families[i].name == SETTINGS.sdFontFamilyName)
        return static_cast<int>(CrossPointSettings::BUILTIN_FONT_COUNT + i);
  }
  return 0;
}

bool EpubReaderMenuActivity::openFontPane(Pane next) {
  {
    RenderLock lock;
    if (sdFontSystem.refreshIfDirty()) {
      if (next == Pane::FontSize) fontSizes = readerFontPointSizes(&sdFontSystem.registry(), SETTINGS.sdFontFamilyName);
      rootFontNav = UiTabListActivity::activeNav();
      pane = next;
      selectedFontRow = currentFontRow();
      UiTabListActivity::activeNav().reset(selectedFontRow + 1);
      app.clearTapFlash();
      requestUpdate();
      return true;
    }
  }
  const char* options[] = {tr(STR_OK_BUTTON)};
  optionPopup.showMessage(tr(STR_FONT_LIST_REFRESH_FAILED), tr(STR_FONT_SETTINGS_REOPEN), options, 1, 0, [](int) {});
  requestUpdate();
  return false;
}

void EpubReaderMenuActivity::closeFontPane() {
  UiTabListActivity::activeNav() = rootFontNav;
  pane = Pane::Root;
  app.clearTapFlash();
  requestUpdate();
}

void EpubReaderMenuActivity::provideFontRow(void* context, uint16_t index, fui::ListItem& item) {
  auto* self = static_cast<EpubReaderMenuActivity*>(context);
  item = {};
  if (index >= self->listCount()) return;
  if (self->pane == Pane::FontSize) {
    snprintf(self->fontSizeLabel, sizeof(self->fontSizeLabel), "%u pt", self->fontSizes[index]);
    item.label = self->fontSizeLabel;
  } else if (index < CrossPointSettings::BUILTIN_FONT_COUNT) {
    item.label = tr(STR_NOTO_SANS);
  } else {
    const auto& name = sdFontSystem.registry().getFamilies()[index - CrossPointSettings::BUILTIN_FONT_COUNT].name;
    item.label = name.c_str();
    item.enabled = name.size() < sizeof(SETTINGS.sdFontFamilyName);
  }
  item.value = index == self->selectedFontRow ? tr(STR_SELECTED) : nullptr;
  item.actionValue = static_cast<int16_t>(index);
}

void EpubReaderMenuActivity::selectFontRow(int index) {
  if (index < 0 || index >= listCount() || sdFontSystem.registryNeedsRefresh()) return;
  if (pane == Pane::FontSize) {
    applyInlineTextSetting(&CrossPointSettings::fontPointSize, fontSizes[index]);
  } else {
    const bool builtin = index < CrossPointSettings::BUILTIN_FONT_COUNT;
    const char* name =
        builtin ? ""
                : sdFontSystem.registry().getFamilies()[index - CrossPointSettings::BUILTIN_FONT_COUNT].name.c_str();
    if (std::strlen(name) >= sizeof(SETTINGS.sdFontFamilyName)) {
      LOG_ERR("MENU", "Font family name exceeds saved settings capacity");
      return;
    }
    {
      RenderLock lock;
      if (std::strcmp(SETTINGS.sdFontFamilyName, name) == 0 && (!builtin || SETTINGS.fontFamily == 0)) return;
      std::memcpy(SETTINGS.sdFontFamilyName, name, std::strlen(name) + 1);
      if (builtin) SETTINGS.fontFamily = 0;
    }
    if (textSettingsChanged) textSettingsChanged();
    SETTINGS.saveToFile();
  }
  {
    RenderLock lock;
    selectedFontRow = currentFontRow();
  }
  app.clearTapFlash();
  requestUpdate();
}

void EpubReaderMenuActivity::applyInlineTextSetting(uint8_t CrossPointSettings::* field, uint8_t value) {
  {
    RenderLock lock;
    if (SETTINGS.*field == value) return;
    SETTINGS.*field = value;
  }
  if (textSettingsChanged) textSettingsChanged();
  SETTINGS.saveToFile();
  requestUpdate();
}

bool EpubReaderMenuActivity::handleInlineTextSetting(MenuAction action) {
  const auto* control = textControl(action);
  if (!tabbed || !control) return false;
  if (control->count == 0) {
    applyInlineTextSetting(control->field, SETTINGS.*(control->field) ? 0 : 1);
    return true;
  }
  const int current = std::clamp((static_cast<int>(SETTINGS.*(control->field)) - control->minimum) / control->step, 0,
                                 static_cast<int>(control->count) - 1);
  auto selected = [this, control](int index) {
    if (index < 0 || index >= control->count) return;
    applyInlineTextSetting(control->field, control->minimum + index * control->step);
  };
  if (control->ids)
    optionPopup.show(control->title, control->ids, control->count, current, std::move(selected));
  else
    optionPopup.show(I18N.get(control->title), control->text, control->count, current, std::move(selected));
  requestUpdate();
  return true;
}

void EpubReaderMenuActivity::updateInlineTextRow(MenuAction action, fui::ListItem& item) {
  const auto* control = textControl(action);
  if (!control) return;
  if (control->count == 0) {
    GUI.setCheckboxRow(item, SETTINGS.*(control->field) != 0);
    return;
  }
  const int index = std::clamp((static_cast<int>(SETTINGS.*(control->field)) - control->minimum) / control->step, 0,
                               static_cast<int>(control->count) - 1);
  item.value = control->ids ? I18N.get(control->ids[index]) : control->text[index];
}

bool EpubReaderMenuActivity::handleHomeGesture() {
  closeCancelled();
  return true;
}

void EpubReaderMenuActivity::activateIndex(const int index) {
  if (optionPopup.isActive() || index < 0 || index >= listCount()) return;
  if (pane != Pane::Root) {
    selectFontRow(index);
    return;
  }
  // The activated row leaves this screen (popup or finish); a lingering flash
  // would gray an unrelated element on the next render.
  app.clearTapFlash();
  activeNav().selected = index + (tabbed ? 1 : 0);

  const auto selectedAction = visibleAction(index);
  if (tabbed && (selectedAction == MenuAction::TEXT_SETTINGS || selectedAction == MenuAction::FONT_SIZE)) {
    openFontPane(selectedAction == MenuAction::TEXT_SETTINGS ? Pane::FontFamily : Pane::FontSize);
    return;
  }
  if (handleInlineTextSetting(selectedAction)) return;
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
  if (pane != Pane::Root && sdFontSystem.registryNeedsRefresh()) {
    RenderLock lock;
    closeFontPane();
    return true;
  }
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool EpubReaderMenuActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (pane != Pane::Root) {
      RenderLock lock;
      closeFontPane();
    } else
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

  if (pane != Pane::Root) {
    fui::ListProps props;
    props.rowProvider = provideFontRow;
    props.rowProviderCtx = this;
    props.count = static_cast<uint16_t>(listCount());
    props.action = ACTION_ROW;
    props.inputMask = fui::InputTouch;
    props.labelText = screen.theme().smallText;
    props.labelText.maxLines = 2;
    syncTabListViewport(screen, props);
    screen.list(props);
    return;
  }

  // menuRowItems's labels/actionValue were set once in the constructor (see
  // buildMenuRowItems()); only rows with live values need refreshing here.
  for (int i = 0; i < visibleCount; ++i) {
    const auto action = visibleAction(i);
    updateInlineTextRow(action, menuRowItems[i]);
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
  const char* caption = pane == Pane::FontFamily ? tr(STR_FONT_FAMILY)
                        : pane == Pane::FontSize ? tr(STR_FONT_SIZE)
                                                 : title.c_str();
  GUI.drawHeader(renderer, UITheme::getInstance().getHeaderRect(renderer, true, false), caption);
}

void EpubReaderMenuActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;

  UiListActivity::render(std::move(lock));
}
