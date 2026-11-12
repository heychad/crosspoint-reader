"""Exercise actual preference capture/save and lifecycle methods under RenderLock."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
saves = source[source.index('bool EpubReaderActivity::saveBookReaderSettings()'):
               source.index('ReaderRenderSpec EpubReaderActivity::effectiveRenderSpec(')]
lifecycle = source[source.index('void EpubReaderActivity::onExit()'):
                   source.index('#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)',
                                source.index('void EpubReaderActivity::onExit()'))]
program = r'''
#include <cassert>
#include <vector>
#define LOG_ERR(...) do {} while (0)
int lockDepth = 0, locks = 0, captures = 0, exits = 0, sleeps = 0;
struct RenderLock {
  RenderLock() { assert(lockDepth == 0); ++lockDepth; ++locks; }
  ~RenderLock() { --lockDepth; }
};
struct BookReaderSettings { int value = 0; };
struct {
  BookReaderSettings captureBookReaderSettings() {
    assert(lockDepth == 1); ++captures; return {captures};
  }
  void endBookReaderSettings() { assert(lockDepth == 1); ++exits; }
} SETTINGS;
struct ReaderActivity {
  void onExit() { assert(lockDepth == 1); }
  void prepareForSleep() { assert(lockDepth == 1); ++sleeps; }
};
struct EpubReaderActivity : ReaderActivity {
  bool bookSettingsActive = true;
  struct {
    bool succeeds = true; std::vector<int> depths;
    bool save(const BookReaderSettings& value) {
      assert(value.value == captures); depths.push_back(lockDepth); return succeeds;
    }
  } bookPreferences;
  struct { bool value = false; void store(bool failed) { value = failed; } } pendingBookSettingsSaveError;
  bool saveBookReaderSettings();
  bool saveBookReaderSettingsLocked();
  bool persistBookReaderSettings(const BookReaderSettings&);
  void onExit(); void prepareForSleep();
};
''' + saves + lifecycle + r'''
int main() {
  EpubReaderActivity reader;
  assert(reader.saveBookReaderSettings());
  assert(captures == 1 && locks == 1 && lockDepth == 0 && reader.bookPreferences.depths.back() == 0);
  {
    RenderLock held;
    assert(reader.saveBookReaderSettingsLocked());
    assert(captures == 2 && locks == 2 && reader.bookPreferences.depths.back() == 1);
    reader.prepareForSleep();
    assert(captures == 3 && locks == 2 && sleeps == 1);
    reader.bookPreferences.succeeds = false;
    reader.onExit();
    assert(captures == 4 && locks == 2 && exits == 1 && !reader.bookSettingsActive);
    assert(reader.pendingBookSettingsSaveError.value);
  }
  assert(reader.saveBookReaderSettings());
  assert(captures == 4 && locks == 2 && lockDepth == 0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Preference capture locks, lifecycle ownership, unlocked writes, and errors passed')
