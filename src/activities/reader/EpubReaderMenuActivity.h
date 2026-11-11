#pragma once
#include <Epub.h>
#include <I18n.h>

#include <functional>
#include <string>
#include <vector>

#include "activities/UiTabListActivity.h"
#include "activities/settings/TextSettingsPreview.h"
#include "components/OptionPopup.h"

class CrossPointSettings;

class EpubReaderMenuActivity final : public UiTabListActivity {
 public:
  // Menu actions available from the reader menu.
  enum class MenuAction {
    SELECT_CHAPTER,
    FOOTNOTES,
    TEXT_SETTINGS,
    RESET_BOOK_SETTINGS,
    NIGHT_MODE,
    FRONTLIGHT,
    BLUETOOTH,
    GO_TO_PERCENT,
    AUTO_PAGE_TURN,
    ROTATE_SCREEN,
    BOOKMARKS,
    SAVE_CLIPPING,
    VIEW_CLIPPINGS,
    TOGGLE_BOOKMARK,
    SCREENSHOT,
    DISPLAY_QR,
    GO_HOME,
    SYNC,
    DELETE_CACHE,
    DICTIONARY,
    READING_STATISTICS,
    READER_BARS,
    SAVED_ITEMS,
    LIBRARY,
    RENDER_MODE,
    FILE_BROWSER,
    FILE_TRANSFER,
    HOME_BUTTON,
    CONTROLS,
    QUICK_ACTIONS,
    TOGGLE_COMPLETION,
    FONT_SIZE,
    LAYOUT_SETTINGS,
    STYLE_SETTINGS,
    LINE_SPACING,
    WORD_SPACING,
    CHARACTER_SPACING,
    TEXT_AA,
    FOCUS_READING,
    SCREEN_MARGIN,
    ALIGNMENT,
    HYPHENATION,
    PARAGRAPH_SPACING,
    PARAGRAPH_INDENT,
    EMBEDDED_STYLE,
    IMAGES,
    TRACK_BOOK_STATS,
    RESET_BOOK_STATS,
    RESET_READING_PACE,
    DELETE_BOOKMARKS,
    BOOK_DICTIONARY,
    INDEXING_METHOD,
    VERTICAL_MARGIN
  };

  enum class Tab : uint8_t { Font, Layout, More, Location, Settings, Count };
  enum class Pane : uint8_t { Root, FontFamily, FontSize };
  static Tab tabForAction(MenuAction action);

  struct MenuItem {
    MenuAction action;
    StrId labelId;
  };

  static void toggleBluetooth();
  static void buildMenuItems(std::vector<MenuItem>& items, bool hasFootnotes, bool hasBookmarks, bool hasClippings,
                             bool completionKnown = false, bool completed = false, bool alreadyInReadFolder = false,
                             bool tabbed = false);

  explicit EpubReaderMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& title,
                                  const int currentPage, const int totalPages, const int bookProgressPercent,
                                  const uint8_t currentOrientation, const bool hasFootnotes, bool hasBookmarks,
                                  bool hasClippings, MenuAction& selectedAction, bool completionKnown = false,
                                  bool completed = false, bool alreadyInReadFolder = false,
                                  std::function<void()> textSettingsChanged = {}, std::string previewText = {});

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override;
  bool acceptsRemoteInput() const override { return routingReady(); }

 private:
  // Row storage: menuItems is at most MAX_MENU_ITEMS, so a
  // fixed-capacity array avoids any heap allocation for the row list. Labels
  // are rebuilt on tab changes (buildMenuRowItems()); buildScreen()
  // only refreshes rows whose values reflect live state.
  static constexpr size_t MAX_MENU_ITEMS = 51;
  freeink::ui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
  void buildMenuRowItems();

  int listCount() const override;
  int tabCount() const override { return tabbed ? static_cast<int>(Tab::Count) : 1; }
  int activeTab() const override { return static_cast<int>(tab); }
  const char* tabLabel(int index) const override;
  void onTabAction(int index) override;
  void stepTab(int direction) override;
  freeink::ui::ListNav& activeNav() override;
  void onRowAction(const freeink::ui::ActionEvent& event) override;
  void navigateButtons() override;
  void rebuildVisibleItems();
  MenuAction visibleAction(int row) const { return menuItems[visibleIndexes[row]].action; }
  const bool tabbed;
  Tab tab = Tab::Font;
  Pane pane = Pane::Root;
  freeink::ui::ListNav rootFontNav;
  std::vector<uint8_t> fontSizes;
  int selectedFontRow = 0;
  char fontSizeLabel[12]{};
  char marginLabels[2][4]{};
  bool openFontPane(Pane next);
  void closeFontPane();
  void selectFontRow(int index);
  int currentFontRow() const;
  static void provideFontRow(void* context, uint16_t index, freeink::ui::ListItem& item);
  uint8_t visibleIndexes[MAX_MENU_ITEMS]{};
  int visibleCount = 0;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  // Popup input runs before any button or touch handling.
  bool handleCustomInput() override;
  // Back closes on RELEASE and Confirm activates on RELEASE; everything else
  // (row navigation, page jumps) falls through to the base handler.
  bool handleButtons() override;
  // Header via GUI.drawHeader inside the safe area for the battery indicator.
  void drawChrome() override;
  void drawFooter() override;
  std::string previewText;
  textsettings::PreviewLayout previewLayout;
  Rect previewBounds;

  void closeCancelled();
  bool handleInlineTextSetting(MenuAction action);
  void applyInlineTextSetting(uint8_t CrossPointSettings::* field, uint8_t value);
  void updateInlineTextRow(MenuAction action, freeink::ui::ListItem& item);
  std::function<void()> textSettingsChanged;

  // Fixed menu layout
  std::vector<MenuItem> menuItems;
  MenuAction& selectedAction;

  OptionPopup optionPopup;
  std::string title = "Reader Menu";
  uint8_t pendingOrientation = 0;
  uint8_t selectedPageTurnOption = 0;
  const std::vector<StrId> orientationLabels = {StrId::STR_PORTRAIT, StrId::STR_LANDSCAPE_CW, StrId::STR_INVERTED,
                                                StrId::STR_LANDSCAPE_CCW};
  const std::vector<const char*> pageTurnLabels = {I18N.get(StrId::STR_STATE_OFF), "1", "3", "6", "12"};
  int currentPage = 0;
  int totalPages = 0;
  int bookProgressPercent = 0;
};
