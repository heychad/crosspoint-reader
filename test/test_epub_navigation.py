"""Exercise the firmware's EPUB link lookup and startup position selection."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
epub = (ROOT / "lib/Epub/Epub.cpp").read_text()
reader = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text()
helpers = (ROOT / "lib/FsHelpers/FsHelpers.cpp").read_text()
path_helpers = helpers[helpers.index("namespace FsHelpers {"):helpers.index("\nbool naturalLess(")] + "\n}\n"
link_start = epub.index("int Epub::resolveHrefToSpineIndex(")
link_end = epub.index("\n}\n", link_start) + 2
call_start = reader.index("  bool sameFile =", reader.index("void EpubReaderActivity::navigateToHref("))
call_end = reader.index("\n\n  if (targetSpineIndex", call_start)
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

""" + path_helpers + r"""
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
  int resolveHrefToSpineIndex(const std::string& href, int sourceSpineIndex = -1) const;
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
  int linkTarget(const std::string& hrefStr) const;
};
""" + epub[link_start:link_end] + """
void Reader::loadPosition() {
""" + reader[resume_start:resume_end] + r"""
}
int Reader::linkTarget(const std::string& hrefStr) const {
""" + reader[call_start:call_end] + r"""
  return targetSpineIndex;
}

int main() {
  Metadata metadata;
  Epub book{&metadata,
            {{"part1/chapter.xhtml"}, {"part2/chapter.xhtml"}, {"other.xhtml"}, {"chapter.xhtml"},
             {"part1/read.xhtml"}, {"part1/part#one.xhtml"}, {"part1/appendix.xhtml"}}};
  assert(book.resolveHrefToSpineIndex("part2/chapter.xhtml#note") == 1);
  assert(book.resolveHrefToSpineIndex("part1/chapter.xhtml") == 0);
  assert(book.resolveHrefToSpineIndex("chapter.xhtml") == 3);
  assert(book.resolveHrefToSpineIndex("other.xhtml") == 2);
  assert(book.resolveHrefToSpineIndex("missing.xhtml") == -1);
  assert(book.resolveHrefToSpineIndex("#note") == -1);
  Reader navigator{&book};
  navigator.currentSpineIndex = 4;
  assert(navigator.linkTarget("chapter.xhtml#note") == 0);
  assert(navigator.linkTarget("./chapter.xhtml#note") == 0);
  assert(navigator.linkTarget("../part2/chapter.xhtml#note") == 1);
  assert(navigator.linkTarget("/chapter.xhtml#note") == 3);
  assert(navigator.linkTarget("part%23one.xhtml#note") == 5);
  assert(navigator.linkTarget("unusable/appendix.xhtml#note") == 6);
  assert(navigator.linkTarget("#here") == 4);

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
