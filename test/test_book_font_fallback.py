"""Run the real SD-font loading path with a controlled font registry."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/SdCardFontSystem.cpp').read_text()
start = source.index('void SdCardFontSystem::ensureLoaded(')
end = source.index('void SdCardFontSystem::setupUiFallbacks(', start)
body = source[start:end]
code = r'''
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#define CROSSPOINT_VECTOR_FONTS 1
#define LOG_DBG(...) do {} while (0)
#define LOG_ERR(...) do {} while (0)
struct GfxRenderer {};
struct Settings {
  char sdFontFamilyName[32] = "";
  uint8_t fontPointSize = 23;
  bool active = true;
  bool bookReaderSettingsActive() const { return active; }
  void clearSdFontFamily() { sdFontFamilyName[0] = 0; fontPointSize = 14; }
} SETTINGS;
const uint8_t BUILTIN_READER_POINT_SIZES[] = {12, 14, 16, 18};
uint8_t snapToNearestPointSize(const uint8_t*, size_t, uint8_t) { return 14; }
void snapFontPointSizeTo(uint8_t size) { if (size) SETTINGS.fontPointSize = size; }
struct FontFile { uint8_t pointSize = 16; };
struct Family { bool vector = false; FontFile file; const FontFile* findNearestSize(uint8_t) const { return &file; } };
struct Registry {
  bool available = false;
  Family family;
  void discover() {}
  const Family* findFamily(const char*) const { return available ? &family : nullptr; }
};
struct Manager {
  std::string name;
  uint8_t size = 12;
  bool canLoad = false;
  const std::string& currentFamilyName() const { return name; }
  uint8_t currentPointSize() const { return size; }
  bool loadFamily(const Family&, GfxRenderer&, uint8_t) {
    if (!canLoad) return false;
    name = SETTINGS.sdFontFamilyName; size = 16; return true;
  }
};
struct SdCardFontSystem {
  std::atomic<bool> registryDirty_{false};
  Registry registry_;
  Manager manager_;
  std::string ttfFamily_;
  int ttfFontId_ = 0;
  void ensureLoaded(GfxRenderer&);
  void unloadCpfonts(GfxRenderer&) { manager_.name.clear(); }
  void unloadTtf(GfxRenderer&) { ttfFamily_.clear(); ttfFontId_ = 0; }
  void loadTtfFamily(const Family&, GfxRenderer&, bool) { SETTINGS.fontPointSize = 16; ttfFontId_ = 0; }
  void setupUiFallbacks(GfxRenderer&) {}
};
''' + body + r'''
int main() {
  GfxRenderer renderer; SdCardFontSystem fonts;
  std::strcpy(SETTINGS.sdFontFamilyName, "MyFont");
  fonts.ensureLoaded(renderer);
  if (std::strcmp(SETTINGS.sdFontFamilyName, "MyFont") || SETTINGS.fontPointSize != 23) return 1;
  fonts.registry_.available = true; fonts.manager_.name = "MyFont"; fonts.manager_.canLoad = false;
  fonts.ensureLoaded(renderer);
  if (std::strcmp(SETTINGS.sdFontFamilyName, "MyFont") || SETTINGS.fontPointSize != 23) return 2;
  fonts.manager_.canLoad = true;
  fonts.ensureLoaded(renderer);
  if (fonts.manager_.name != "MyFont" || SETTINGS.fontPointSize != 16) return 3;
  SETTINGS.fontPointSize = 23; fonts.registry_.family.vector = true;
  fonts.ensureLoaded(renderer);
  if (std::strcmp(SETTINGS.sdFontFamilyName, "MyFont") || SETTINGS.fontPointSize != 23) return 5;
  fonts.registry_.family.vector = false;
  SETTINGS.active = false; SETTINGS.fontPointSize = 23; fonts.registry_.available = false;
  fonts.ensureLoaded(renderer);
  if (SETTINGS.sdFontFamilyName[0] || SETTINGS.fontPointSize != 14) return 4;
  std::puts("SD font fallback checks passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
