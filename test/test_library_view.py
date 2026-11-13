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
#include "personalization/LibraryViewSettings.h"
enum class Order { RecentAsc, RecentDesc, TitleAsc, TitleDesc, AuthorAsc, AuthorDesc,
                   SeriesAsc, SeriesDesc, GenreAsc, GenreDesc };
constexpr int TAB_SLOTS = 5;
constexpr Order orderForTab(int tab, uint8_t bits) {
  return static_cast<Order>(tab * 2 + ((bits >> tab) & 1));
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
struct LibraryListActivity {
  int activeTabIndex = 0;
  uint8_t descendingTabs = 1;
  Order sortOrder = Order::RecentDesc;
  int filters = 0, overlaps = 0, paints = 0, swipesClosed = 0;
  struct Nav { int selected = 4, top = 3; } nav;
  int activeTab() const { return activeTabIndex; }
  Nav& activeNav() { return nav; }
  void closeSwipeDelete() { ++swipesClosed; }
  void applyFilter() { ++filters; }
  void refreshOverlap() { ++overlaps; }
  void requestUpdate() { ++paints; }
  void loadViewPreferences();
  void selectTab(int, bool);
  void toggleSortDirection();
};
// INSERT_METHODS
int main() {
  for (unsigned tab = 0; tab < 5; ++tab) {
    for (unsigned bits = 0; bits < 32; ++bits) {
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
      assert(view.activeTab() == tab && view.sortOrder == orderForTab(tab, bits));
      int writes = SETTINGS.writes;
      view.selectTab(tab, false);
      assert(SETTINGS.writes == writes);
      view.selectTab(-1, true);
      view.selectTab(5, true);
      assert(SETTINGS.writes == writes && view.activeTab() == tab);
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
      assert(reopened.activeTab() == tab && reopened.sortOrder == view.sortOrder);
      for (unsigned next = 0; next < 5; ++next) {
        writes = SETTINGS.writes;
        const int previous = reopened.activeTab();
        const auto directions = reopened.descendingTabs;
        reopened.selectTab(next, false);
        assert(reopened.activeTab() == next && reopened.descendingTabs == directions);
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
      assert(loaded.descendingTabs == (key == std::string_view("libraryViewDescendingTabs") ? 1 : 31));
    }
  }
  for (unsigned bad = 5; bad < 256; ++bad) {
    JsonDocument doc;
    doc["libraryViewTab"] = bad;
    doc["libraryViewDescendingTabs"] = bad;
    libraryview::Preferences loaded;
    loaded.read(doc.as<JsonVariantConst>());
    assert(loaded.tab == 0 && loaded.descendingTabs == (bad < 32 ? bad : 1));
  }
  JsonDocument empty;
  empty.to<JsonObject>();
  libraryview::Preferences defaults;
  defaults.tab = 4;
  defaults.descendingTabs = 31;
  defaults.read(empty.as<JsonVariantConst>());
  assert(defaults.tab == 0 && defaults.descendingTabs == 1);
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
}
'''
methods = '\n'.join(method(signature) for signature in (
    'void LibraryListActivity::loadViewPreferences(',
    'void LibraryListActivity::selectTab(',
    'void LibraryListActivity::toggleSortDirection('))
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    source_path = directory / 'test.cpp'
    binary = directory / 'test'
    source_path.write_text(program.replace('// INSERT_METHODS', methods))
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(source_path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Library views restore, toggle, preserve other directions, and retain failed saves for retry')
