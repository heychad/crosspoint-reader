"""Exercise the production EPUB metadata loader's cache and source fallbacks."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/Epub/Epub.cpp').read_text()
start = source.index('bool Epub::loadMetadata(')
end = source.index('\nbool Epub::loadSyncMetadata(', start)
method = source[start:end]
code = r'''
#include <cassert>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
bool cacheReady = true, zipReady = true, parseReady = true;
int cacheReads = 0, zipOpens = 0;
struct BookMetadataCache {
  struct BookMetadata { std::string title, author; } coreMetadata;
  explicit BookMetadataCache(const std::string&) {}
  bool load() {
    ++cacheReads;
    coreMetadata = {"Cached title", "Cached author"};
    return cacheReady;
  }
};
template <class T, class... Args>
auto makeUniqueNoThrow(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }
struct ZipFile {
  explicit ZipFile(const std::string&) {}
  bool open() { ++zipOpens; return zipReady; }
  void close() {}
};
namespace Txt { bool isTxtOrMd(const std::string& path) { return path.ends_with(".txt"); } }
namespace FsHelpers { std::string getFileNameWithoutExtension(const std::string&) { return "Plain title"; } }
std::string utf8ComposeNfc(const std::string& value) { return value; }
struct Epub {
  std::string filepath = "book.epub", cachePath = "/cache";
  bool loadMetadata(std::string&, std::string&, std::string* = nullptr, std::string* = nullptr,
                    std::optional<float>* = nullptr);
  bool parseContentOpf(BookMetadataCache::BookMetadata& metadata, bool write, bool only, ZipFile*,
                       std::string* series, std::string* genre, std::optional<float>* position) {
    assert(!write && only);
    if (!parseReady) return false;
    metadata = {"Source title", "Source author"};
    if (series) *series = "Source series";
    if (genre) *genre = "Source genre";
    if (position) *position = 2.5f;
    return true;
  }
};
// INSERT_METHOD
int main() {
  Epub book;
  std::string title, author, series, genre;
  std::optional<float> position;
  assert(book.loadMetadata(title, author));
  assert(title == "Cached title" && zipOpens == 0);
  assert(book.loadMetadata(title, author, &series, &genre, &position));
  assert(title == "Source title" && series == "Source series" && genre == "Source genre" && position == 2.5f);
  zipReady = false;
  assert(book.loadMetadata(title, author, &series, &genre, &position));
  assert(title == "Cached title" && author == "Cached author" && series.empty() && genre.empty() && !position);
  zipReady = true;
  parseReady = false;
  assert(book.loadMetadata(title, author, &series, &genre, &position));
  assert(title == "Cached title" && series.empty() && genre.empty());
  cacheReady = false;
  assert(!book.loadMetadata(title, author, &series, &genre, &position));
  assert(title.empty() && author.empty() && series.empty() && genre.empty() && !position);
  book.filepath = "notes.txt";
  assert(book.loadMetadata(title, author, &series, &genre, &position));
  assert(title == "Plain title" && author.empty());
}
'''.replace('// INSERT_METHOD', method)
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(directory / 'test.cpp'), '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test')], check=True)
print('EPUB metadata source, cache fallback and existing basic-loader behavior passed')
