#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace freeink::ui {
struct ListItem {
  const char* label = nullptr;
  const char* value = nullptr;
  int icon = 0, actionValue = 0;
};
struct TextStyle {
  int maxLines = 0;
};
}  // namespace freeink::ui
namespace fui = freeink::ui;
struct Settings {
  bool hideFileExtension = false;
  int fileBrowserDisplay = 1;
} SETTINGS;
struct UITheme {
  static UITheme& getInstance() {
    static UITheme theme;
    return theme;
  }
  UITheme& getTheme() { return *this; }
  bool showsFileIcons() { return true; }
  static int getFileIcon(const std::string&) { return 1; }
};
int listIconFor(int icon) { return icon; }
void utf8ComposeNfcInPlace(char*) {}
struct FileBrowserActivity {
  enum class Mode { Books, PickFirmware };
  Mode mode = Mode::Books;
  std::vector<std::string> files = {"name.epub", "folder/"};
  char rowNameBuf[512]{}, rowExtBuf[16]{};
  static void provideRow(void*, uint16_t, freeink::ui::ListItem&);
  int lines() {
    struct Screen {
      struct Theme {
        fui::TextStyle smallText;
      };
      Theme theme() { return {}; }
    } screen;
    struct Props {
      fui::TextStyle labelText;
    } props;

    // INSERT_LAYOUT
    return props.labelText.maxLines;
  }
};

// INSERT_METHODS
int main() {
  FileBrowserActivity browser;
  freeink::ui::ListItem item;
  browser.provideRow(&browser, 0, item);
  assert(std::string(item.label) == "name" && std::string(item.value) == ".epub");
  SETTINGS.hideFileExtension = true;
  browser.provideRow(&browser, 0, item);
  assert(item.value == nullptr);
  browser.mode = FileBrowserActivity::Mode::PickFirmware;
  browser.provideRow(&browser, 0, item);
  assert(std::string(item.value) == ".epub");
  browser.provideRow(&browser, 1, item);
  assert(std::string(item.label) == "folder" && item.value == nullptr);
  for (int mode = 0; mode < 2; ++mode) {
    SETTINGS.fileBrowserDisplay = mode;
    browser.mode = FileBrowserActivity::Mode::Books;
    assert(browser.lines() == (mode == 0 ? 1 : 2));
    browser.mode = FileBrowserActivity::Mode::PickFirmware;
    assert(browser.lines() == 2);
  }
}
