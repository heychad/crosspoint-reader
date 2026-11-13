"""Exercise production Library view changes with host UI/storage stand-ins."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()


def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


# Reopening restores the view before the index filter or first paint can use it.
entry = method('void LibraryListActivity::onEnter(')
assert entry.index('loadViewPreferences();') < entry.index('UiTabListActivity::onEnter();')
assert entry.index('loadViewPreferences();') < entry.index('applyFilter();')
settings = (ROOT / 'src/CrossPointSettings.cpp').read_text()
assert 'libraryView.write(doc);' in settings
assert 'libraryView.read(doc);' in settings
program = r'''
#include <cassert>
#include <cstdint>
#include <string_view>
#include <functional>
#include <memory>
#include <span>
#include <variant>
#include "personalization/LibraryViewSettings.h"
enum class Order { RecentAsc, RecentDesc, TitleAsc, TitleDesc, AuthorAsc, AuthorDesc,
                   SeriesAsc, SeriesDesc, GenreAsc, GenreDesc, AuthorFirstAsc, AuthorFirstDesc, DateAddedAsc, DateAddedDesc };
constexpr int TAB_SLOTS = 5;
constexpr Order orderForTab(int tab, uint8_t bits) {
  return static_cast<Order>((tab == 6 ? 0 : tab == 7 ? 12 : tab * 2) + ((bits >> tab) & 1));
}
#define FREEINK_CAP_TOUCH 1
#define LOG_ERR(...) ((void)0)
struct RenderLock { template<typename T> explicit RenderLock(T&) {} };
struct Settings {
  libraryview::Preferences libraryView;
  JsonDocument disk;
  int writes = 0;
  bool fail = false;
  bool saveToFile() {
    ++writes;
    if (fail) return false;
    libraryView.write(disk);
    return true;
  }
} SETTINGS;
enum class StrId { STR_LIBRARY_REVERSE_ORDER, STR_LIBRARY_TAB_RECENT, STR_LIBRARY_TAB_TITLE,
                   STR_LIBRARY_AUTHOR_SURNAME, STR_LIBRARY_TAB_SERIES, STR_LIBRARY_TAB_GENRE,
                   STR_LIBRARY_RECENT_AND_NEW, STR_LIBRARY_RECENTLY_OPENED,
                   STR_LIBRARY_AUTHOR_FIRST_NAME, STR_LIBRARY_DATE_ADDED, STR_LIBRARY_REBUILD, STR_LIBRARY_SORT };
struct MenuResult { int action; };
struct ActivityResult { bool isCancelled = false; std::variant<std::monostate, MenuResult> data; };
struct EnumSelectionActivity {
  std::span<const StrId> labels;
  int selected;
  EnumSelectionActivity(int&, int&, StrId, std::span<const StrId> labels, int selected)
      : labels(labels), selected(selected) {}
};
bool failPicker = false;
template<class T, class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return failPicker ? nullptr : std::make_unique<T>(std::forward<Args>(args)...);
}
struct LibraryListActivity {
  int activeTabIndex = 0;
  uint8_t descendingTabs = 1;
  Order sortOrder = Order::RecentDesc;
  int filters = 0, overlaps = 0, paints = 0, swipesClosed = 0;
  struct Nav { int selected = 4, top = 3; } nav;
  int activeTab() const;
  Nav& activeNav() { return nav; }
  void closeSwipeDelete() { ++swipesClosed; }
  void applyFilter() { ++filters; assert(sortOrder == orderForTab(activeTabIndex, descendingTabs)); }
  void refreshOverlap() { ++overlaps; }
  void requestUpdate() { ++paints; }
  bool historyView() const { return activeTabIndex == 6; }
  void refreshViewIndex() {}
  int resolves = 0;
  void resolvePinned() { ++resolves; }
  void loadViewPreferences();
  void selectTab(int, bool);
  void selectView(int, bool);
  void toggleSortDirection();
  int renderer = 0, mappedInput = 0, starts = 0, swallowed = 0, rebuilds = 0;
  struct App { int clears = 0; void clearTapFlash() { ++clears; } } app;
  std::unique_ptr<EnumSelectionActivity> picker;
  std::function<void(const ActivityResult&)> callback;
  void startActivityForResult(std::unique_ptr<EnumSelectionActivity> child,
                              std::function<void(const ActivityResult&)> onResult) {
    ++starts;
    picker = std::move(child);
    callback = std::move(onResult);
  }
  void swallowHeldReleases() { ++swallowed; }
  void promptRebuildIndex() { ++rebuilds; }
  void openSortPicker();
};
// INSERT_METHODS
int main() {
  for (unsigned tab = 0; tab < 8; ++tab) {
    for (unsigned bits = 0; bits < 256; ++bits) {
      JsonDocument saved;
      saved["keep"] = 42;
      libraryview::Preferences original;
      original.tab = tab;
      original.descendingTabs = bits;
      original.write(saved);
      libraryview::Preferences loaded;
      loaded.read(saved.as<JsonVariantConst>());
      assert(loaded.tab == tab && loaded.descendingTabs == bits && saved["keep"] == 42);
      SETTINGS.libraryView = loaded;
      LibraryListActivity view;
      view.loadViewPreferences();
      assert(view.activeTabIndex == tab && view.activeTab() == libraryview::mainTab(tab) && view.sortOrder == orderForTab(tab, bits));
      int writes = SETTINGS.writes;
      view.selectTab(libraryview::mainTab(tab), false);
      assert(SETTINGS.writes == writes);
      view.selectTab(-1, true);
      view.selectTab(5, true);
      view.selectView(8, true);
      assert(SETTINGS.writes == writes && view.activeTabIndex == tab && view.activeTab() == libraryview::mainTab(tab));
      view.toggleSortDirection();
      assert(view.descendingTabs == (bits ^ (1u << tab)));
      assert(view.sortOrder == orderForTab(tab, view.descendingTabs));
      assert(SETTINGS.libraryView.tab == tab && SETTINGS.libraryView.descendingTabs == view.descendingTabs);
      assert(SETTINGS.writes == writes + 1 && view.nav.selected == 0 && view.nav.top == 0);
      assert(view.filters == 2 && view.overlaps == 2 && view.paints == 2);
      // Recreate settings and activity, as after a clean restart.
      SETTINGS.libraryView = {};
      SETTINGS.libraryView.read(SETTINGS.disk.as<JsonVariantConst>());
      LibraryListActivity reopened;
      reopened.loadViewPreferences();
      assert(reopened.activeTabIndex == tab && reopened.sortOrder == view.sortOrder);
      for (unsigned next = 0; next < 8; ++next) {
        writes = SETTINGS.writes;
        const int previous = reopened.activeTabIndex;
        const auto directions = reopened.descendingTabs;
        const int resolves = reopened.resolves;
        reopened.selectView(next, false);
        if (previous == 6 && next != 6) assert(reopened.resolves == resolves + 1);
        assert(reopened.activeTabIndex == next && reopened.descendingTabs == directions);
        assert(SETTINGS.writes == writes + (previous != next));
        assert(reopened.sortOrder == orderForTab(next, directions));
      }
    }
  }
  for (const char* key : {"libraryViewTab", "libraryViewDescendingTabs"}) {
    for (const char* invalid : {"-1", "256", "1.0", "true", "null", "\"1\"", "[]", "{}"}) {
      JsonDocument doc, bad;
      doc["libraryViewTab"] = 4;
      doc["libraryViewDescendingTabs"] = 31;
      assert(!deserializeJson(bad, invalid));
      doc[key] = bad.as<JsonVariantConst>();
      libraryview::Preferences loaded;
      loaded.read(doc.as<JsonVariantConst>());
      assert(loaded.tab == (key == std::string_view("libraryViewTab") ? 0 : 4));
      assert(loaded.descendingTabs == (key == std::string_view("libraryViewDescendingTabs") ? 193 : 31));
    }
  }
  for (unsigned bad = 8; bad < 256; ++bad) {
    JsonDocument doc;
    doc["libraryViewTab"] = bad;
    doc["libraryViewDescendingTabs"] = bad;
    libraryview::Preferences loaded;
    loaded.read(doc.as<JsonVariantConst>());
    assert(loaded.tab == 0 && loaded.descendingTabs == bad);
  }
  JsonDocument empty;
  empty.to<JsonObject>();
  libraryview::Preferences defaults;
  defaults.tab = 4;
  defaults.descendingTabs = 31;
  defaults.read(empty.as<JsonVariantConst>());
  assert(defaults.tab == 0 && defaults.descendingTabs == 193);
  SETTINGS.libraryView = {};
  assert(SETTINGS.saveToFile());
  SETTINGS.fail = true;
  LibraryListActivity failed;
  failed.loadViewPreferences();
  failed.selectTab(4, false);
  assert(failed.activeTab() == 4 && SETTINGS.libraryView.tab == 4);
  assert(SETTINGS.disk["libraryViewTab"] == 0);
  // The existing settings retry saves the current RAM choice.
  SETTINGS.fail = false;
  assert(SETTINGS.saveToFile());
  SETTINGS.libraryView = {};
  SETTINGS.libraryView.read(SETTINGS.disk.as<JsonVariantConst>());
  failed.loadViewPreferences();
  assert(failed.activeTab() == 4);

  LibraryListActivity picker;
  picker.selectView(5, false);
  picker.openSortPicker();
  assert(picker.starts == 1 && picker.picker->selected == 6 && picker.picker->labels.size() == 10);
  assert(picker.picker->labels[0] == StrId::STR_LIBRARY_REVERSE_ORDER);
  assert(picker.picker->labels[6] == StrId::STR_LIBRARY_AUTHOR_FIRST_NAME);
  assert(picker.picker->labels[7] == StrId::STR_LIBRARY_RECENTLY_OPENED && picker.picker->labels[8] == StrId::STR_LIBRARY_DATE_ADDED && picker.picker->labels[9] == StrId::STR_LIBRARY_REBUILD);
  const auto writes = SETTINGS.writes;
  picker.callback({true, MenuResult{1}});
  picker.callback({false, std::monostate{}});
  picker.callback({false, MenuResult{-1}});
  picker.callback({false, MenuResult{10}});
  assert(SETTINGS.writes == writes && picker.activeTabIndex == 5 && picker.swallowed == 4);
  for (int choice = 1; choice <= 8; ++choice) {
    picker.callback({false, MenuResult{choice}});
    assert(picker.activeTabIndex == choice - 1);
    assert(picker.activeTab() == libraryview::mainTab(choice - 1));
    assert(SETTINGS.libraryView.tab == choice - 1);
  }
  picker.selectView(5, false);
  const auto previous = picker.descendingTabs;
  picker.callback({false, MenuResult{0}});
  assert(picker.descendingTabs == (previous ^ 32) && picker.activeTabIndex == 5);
  picker.callback({false, MenuResult{9}});
  assert(picker.rebuilds == 1);
  // The alias uses the Author tab's navigation, while keeping its own direction.
  const auto mask = picker.descendingTabs;
  picker.selectTab(2, true);
  assert(picker.activeTabIndex == 5 && picker.descendingTabs == (mask ^ 32));
  picker.selectTab(1, false);
  picker.selectTab(2, false);
  assert(picker.activeTabIndex == 2 && picker.activeTab() == 2);
  failPicker = true;
  picker.openSortPicker();
  assert(picker.starts == 1 && picker.app.clears == 1);
}
'''
methods = '\n'.join(method(signature) for signature in (
    'void LibraryListActivity::loadViewPreferences(',
    'void LibraryListActivity::selectTab(',
    'void LibraryListActivity::selectView(',
    'int LibraryListActivity::activeTab(',
    'void LibraryListActivity::openSortPicker(',
    'void LibraryListActivity::toggleSortDirection('))
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    source_path = directory / 'test.cpp'
    binary = directory / 'test'
    source_path.write_text(program.replace('// INSERT_METHODS', methods))
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(source_path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Library views restore, toggle, preserve other directions, and retain failed saves for retry')
