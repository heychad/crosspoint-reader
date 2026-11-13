"""Exercise production recent-history loading and rotation at the RC2 limit."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/RecentBooksStore.cpp').read_text()
header = (ROOT / 'src/RecentBooksStore.h').read_text()
capacity = int(re.search(r'MAX_RECENT_BOOKS = (\d+)', header).group(1))
assert capacity == 18


def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


program = r'''
#include <ArduinoJson.h>
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
#define LOG_DBG(...) ((void)0)
struct RecentBook {
  std::string path, title, author, coverBmpPath;
};
struct RecentBooksStore {
  static constexpr int MAX_RECENT_BOOKS = CAPACITY;
  std::vector<RecentBook> recentBooks;
  JsonDocument disk;
  int writes = 0;
  int getCount() const { return recentBooks.size(); }
  bool pruneMissing() { return false; }
  bool saveToFile() { ++writes; disk.clear(); toJson(disk); return true; }
  bool fromJson(JsonVariantConst);
  void toJson(JsonDocument&) const;
  void addBook(const std::string&, const std::string&, const std::string&, const std::string&);
};
// INSERT_METHODS
int main() {
  for (int count : {10, 18, 25}) {
    JsonDocument original;
    for (int i = 0; i < count; ++i) {
      auto book = original["books"].add<JsonObject>();
      book["path"] = "/book" + std::to_string(i) + ".epub";
      book["title"] = "Title " + std::to_string(i);
      book["author"] = "Author";
      book["coverBmpPath"] = "/cover" + std::to_string(i) + ".bmp";
    }
    const auto before = original.size();
    RecentBooksStore store;
    assert(store.fromJson(original.as<JsonVariantConst>()));
    assert(store.getCount() == std::min(count,18) && store.writes == 0 && original.size() == before);
    for (int i = 0; i < store.getCount(); ++i) {
      assert(store.recentBooks[i].path == "/book" + std::to_string(i) + ".epub");
      assert(store.recentBooks[i].title == "Title " + std::to_string(i));
    }
    const auto oldCount = store.getCount();
    store.addBook("/book8.epub", "Changed", "Updated", "/changed.bmp");
    assert(store.getCount() == oldCount && store.recentBooks[0].path == "/book8.epub");
    assert(store.recentBooks[1].path == "/book0.epub" && store.writes == 1);
    store.addBook("/new.epub", "New", "Author", "/new.bmp");
    assert(store.getCount() == std::min(oldCount + 1,18) && store.recentBooks[0].path == "/new.epub");
    RecentBooksStore reopened;
    assert(reopened.fromJson(store.disk.as<JsonVariantConst>()));
    assert(reopened.getCount() == store.getCount());
    for (int i = 0; i < reopened.getCount(); ++i)
      assert(reopened.recentBooks[i].path == store.recentBooks[i].path);
  }
}
'''
methods = '\n'.join(method(signature) for signature in (
    'bool RecentBooksStore::fromJson(', 'void RecentBooksStore::toJson(', 'void RecentBooksStore::addBook('))
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    cpp = directory / 'test.cpp'
    binary = directory / 'test'
    cpp.write_text(program.replace('CAPACITY', str(capacity)).replace('// INSERT_METHODS', methods))
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Recent history preserves 10/18-entry loads, caps larger input, rotates, and survives serialization')
