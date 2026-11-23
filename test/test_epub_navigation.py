"""Exercise the firmware's EPUB link lookup and startup position selection."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
epub = (ROOT / "lib/Epub/Epub.cpp").read_text()
reader = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()
link_start = epub.index("int Epub::resolveHrefToSpineIndex(")
link_end = epub.index("\n}\n", link_start) + 2
resume_start = reader.index("  epub->setupCacheDir();", reader.index("bool EpubReaderActivity::loadBook()"))
resume_end = reader.index("\n  loadLinkStack();", resume_start)

program = r"""
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#define LOG_DBG(...)

namespace FsHelpers {
std::string decodeUriEscapes(const std::string& path) { return path; }
std::string normalisePath(const std::string& path) { return path; }
}

struct Metadata {
  bool isLoaded() const { return true; }
};
struct SpineItem {
  std::string href;
};
struct Epub {
  const Metadata* bookMetadataCache = nullptr;
  std::vector<SpineItem> spine;
  int guideReads = 0;
  int getSpineItemsCount() const { return static_cast<int>(spine.size()); }
  SpineItem getSpineItem(int i) const { return spine.at(i); }
  int resolveHrefToSpineIndex(const std::string& href) const;
  void setupCacheDir() {}
  std::string getCachePath() const { return "/cache"; }
  int getSpineIndexForTextReference() { ++guideReads; return 2; }
};

struct HalFile {
  std::vector<uint8_t> bytes;
  int read(uint8_t* out, size_t capacity) {
    const size_t count = std::min(bytes.size(), capacity);
    std::copy_n(bytes.begin(), count, out);
    return static_cast<int>(count);
  }
};
struct {
  std::optional<std::vector<uint8_t>> record;
  bool openFileForRead(const char*, const std::string&, HalFile& file) const {
    if (!record) return false;
    file.bytes = *record;
    return true;
  }
} Storage;
struct Reader {
  Epub* epub;
  int currentSpineIndex = 0, nextPageNumber = 0, cachedSpineIndex = -1;
  int cachedChapterTotalPageCount = 0;
  std::optional<uint32_t> cachedVisibleTextOffset;
  void loadPosition();
};
""" + epub[link_start:link_end] + """
void Reader::loadPosition() {
""" + reader[resume_start:resume_end] + r"""
}

int main() {
  Metadata metadata;
  Epub book{&metadata, {{"part1/chapter.xhtml"}, {"part2/chapter.xhtml"}, {"other.xhtml"}}};
  assert(book.resolveHrefToSpineIndex("part2/chapter.xhtml#note") == 1);
  assert(book.resolveHrefToSpineIndex("part1/chapter.xhtml") == 0);
  assert(book.resolveHrefToSpineIndex("chapter.xhtml") == 0);
  assert(book.resolveHrefToSpineIndex("other.xhtml") == 2);
  assert(book.resolveHrefToSpineIndex("missing.xhtml") == -1);
  assert(book.resolveHrefToSpineIndex("#note") == -1);

  for (const size_t size : {4, 6, 10}) {
    Storage.record = std::vector<uint8_t>{0, 0, 3, 0, 6, 0, 24, 0, 0, 0};
    Storage.record->resize(size);
    Reader saved{&book};
    saved.loadPosition();
    assert(saved.currentSpineIndex == 0 && saved.nextPageNumber == 3 && saved.cachedSpineIndex == 0);
    assert(saved.cachedChapterTotalPageCount == (size >= 6 ? 6 : 0));
    assert(saved.cachedVisibleTextOffset == (size == 10 ? std::optional<uint32_t>(24) : std::nullopt));
  }
  assert(book.guideReads == 0);
  Storage.record = std::vector<uint8_t>{1, 0, 3, 0};
  Reader later{&book};
  later.loadPosition();
  assert(later.currentSpineIndex == 1 && book.guideReads == 0);
  Storage.record = std::vector<uint8_t>{0, 0, 3};
  Reader incomplete{&book};
  incomplete.loadPosition();
  assert(incomplete.currentSpineIndex == 2 && book.guideReads == 1);
  Storage.record.reset();
  Reader fresh{&book};
  fresh.loadPosition();
  assert(fresh.currentSpineIndex == 2 && book.guideReads == 2);
}
"""

with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "navigation.cpp"
    binary = Path(directory) / "navigation"
    source.write_text(program)
    subprocess.run(["c++", "-std=c++20", str(source), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
