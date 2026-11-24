"""Exercise explicit EPUB deletion and its directory listing at FAT name limits."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 2]


storage = r'''
#include <ArduinoJson.h>
#include <CheckedStoragePath.h>
#include <HiddenItems.h>
#include <cassert>
#include <cstring>
#include <strings.h>
#include <string>
#include <vector>
#include "util/BookCacheUtils.h"
#include "util/BookDeletionPath.h"
#include "util/BookMove.h"
#include "util/BookMovePlan.h"
#define FREEINK_DEVICE_X4PRO 1
using protectedpaths::BOOK_DELETION_QUEUE;
struct String : std::string {
  using std::string::string;
  bool startsWith(const char* text) const { return starts_with(text); }
  bool equals(const char* text) const { return *this == text; }
};
namespace FsHelpers {
bool hasEpubExtension(std::string_view path) {
  return path.size() >= 5 && !strncasecmp(path.data() + path.size() - 5, ".epub", 5);
}
}
struct Entry { std::string name; uint64_t size; bool directory; };
struct HalFile {
  const Entry* entry = nullptr;
  const std::vector<Entry>* list = nullptr;
  size_t offset = 0;
  bool error = false;
  explicit operator bool() const { return entry || list; }
  HalFile openNextFile() { return offset < list->size() ? HalFile{&list->at(offset++)} : HalFile{}; }
  size_t getName(char* out, size_t size) const {
    if (entry->name.size() >= size) return 0;
    memcpy(out, entry->name.c_str(), entry->name.size()+1);
    return entry->name.size();
  }
  bool isDirectory() const { return list || entry->directory; }
  uint64_t fileSize64() const { return entry->size; }
  bool hasError() const { return error; }
  bool close() { return true; }
};
struct HalStorage {
  enum class PathStatus { Missing, Present, Failed };
  Entry source{"book", 100, false};
  std::vector<Entry> directory;
  bool readError = false;
  HalFile openChecked(const char* path) {
    const std::string_view p(path);
    if (p == "/unreadable" || p == "/missing") return {};
    if (p == "/folder") return {nullptr,&directory,0,readError};
    return {&source};
  }
  PathStatus lookupPath(const char* path) {
    return std::string_view(path) == "/unreadable" ? PathStatus::Failed : PathStatus::Missing;
  }
} Storage;
unsigned hardDeletes = 0;
bool removeBookFile(const std::string&) { ++hardDeletes; return true; }
void esp_fill_random(void* dest, size_t size) { memset(dest, 0xab, size); }
namespace bookmove {
std::string held;
Result move(const std::string&,const std::string& target) { held = target; return Result::Moved; }
}
namespace filechecksum {
'''
wire = r'''
struct Transport {
  std::string body;
  String path;
  int status = 0;
  bool stopped = false;
  Transport& client() { return *this; }
  void stop() { stopped = true; }
  bool hasArg(const char*) const { return true; }
  String arg(const char*) const { return path; }
  void setContentLength(size_t) {}
  void send(int code,const char*,const char* content) { status=code;body=content; }
  void sendContent(const char* bytes) { body += bytes; }
  void sendContent(const char* bytes, size_t size) { body.append(bytes, size); }
};
struct CrossPointWebServer {
  Transport* server;
  void handleFileListData() const;
  bool streamDirectoryEntries(HalFile& directory) const;
  bool isEpubFile(const String& path) const { return FsHelpers::hasEpubExtension(path); }
};
struct { bool showHiddenFiles = false; } SETTINGS;
void yield() {}
void resetTaskWatchdogIfSubscribed() {}
String normalizeWebPath(const String& p){return p;}
#define CONTENT_LENGTH_UNKNOWN 0
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
'''
checks = r'''
std::string repeat(const char* text, unsigned n) {
  std::string result;
  while (n--) result += text;
  return result;
}
int main() {
  const std::string folder = repeat("界",255);
  const std::string leaf = repeat("本",245)+".epub";
  const std::string paths[] = {"/Books/"+std::string(226,'a')+".epub",
                               "/Books/"+std::string(245,'b')+".epub", "/Books/"+leaf,
                               "/"+folder+"/book.epub"};
  for (const auto& path: paths) {
    auto issue = BookDeleteIssue::UnsupportedPath;
    assert(removeBookFileWithReceipt(path,&issue) && issue == BookDeleteIssue::None && hardDeletes == 0);
    bookmove::DeletionPath receipt;
    assert(bookmove::parseDeletionPath(bookmove::held,receipt) && receipt.original == path);
  }
  for (const auto& path: {"/Books/"+std::string(246,'c')+".epub", std::string("/Books/B~1.epub"),
                          "/"+folder+"/"+leaf}) {
    auto issue = BookDeleteIssue::None;
    assert(!removeBookFileWithReceipt(path,&issue) && issue == BookDeleteIssue::UnsupportedPath && hardDeletes == 0);
  }
  assert(removeBookFileWithReceipt("/zDev/local.epub") && hardDeletes == 1);
  Storage.source.size = uint64_t{1} << 32;
  auto tooLarge = BookDeleteIssue::None;
  assert(!removeBookFileWithReceipt("/Books/large.epub",&tooLarge));
  assert(tooLarge == BookDeleteIssue::UnsupportedSize && hardDeletes == 1);
  Storage.source.size = 0;
  assert(removeBookFileWithReceipt("/Books/empty.epub") && hardDeletes == 2);
  assert(removeBookFileWithReceipt("/Books/note.txt") && hardDeletes == 3);

  std::vector<Entry> entries{{folder,0,true},{leaf+".held",100,false},{"normal.epub",10,false},
                             {".private",4,false},{"XTCache",0,true}};
  Transport tx; CrossPointWebServer web{&tx}; HalFile directory{nullptr,&entries};
  assert(web.streamDirectoryEntries(directory));
  JsonDocument result;
  assert(!deserializeJson(result,"["+tx.body+"]") && result.size()==3);
  assert(result[0]["name"].as<std::string>()==folder && result[0]["isDirectory"]==true);
  assert(result[1]["name"].as<std::string>()==leaf+".held" && result[1]["size"]==100);
  assert(result[2]["isEpub"]==true);
  directory.offset = 0; directory.error = true;
  assert(!web.streamDirectoryEntries(directory));
  Storage.directory = entries;
  tx.path = "/folder";
  web.handleFileListData();
  assert(tx.status==200 && !tx.stopped && !deserializeJson(result,tx.body) && result.size()==3);
  Storage.readError = true;
  web.handleFileListData();
  assert(tx.stopped && deserializeJson(result,tx.body));
  tx.path = "/missing";
  web.handleFileListData(); assert(tx.status==200 && tx.body=="[]");
  tx.path = "/unreadable";
  web.handleFileListData(); assert(tx.status==503);

}
'''
checksum = 'src/network/FileChecksum.cpp'
cache = 'src/util/BookCacheUtils.cpp'
server = 'src/network/CrossPointWebServer.cpp'
size_constant = '\n'.join(line for line in (ROOT / 'src/network/FileChecksum.h').read_text().splitlines()
                          if line.startswith(('constexpr size_t MAX_PATH_BYTES', 'constexpr uint64_t MAX_FILE_BYTES')))
source = '\n'.join([storage, size_constant, method(checksum, 'bool validPath('), '}',
                    method(cache, 'bool removeBookFileWithReceipt('), wire,
                    method(server, 'bool CrossPointWebServer::streamDirectoryEntries('),
                    method(server, 'void CrossPointWebServer::handleFileListData('), checks])
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    cpp = path / 'receipt_paths.cpp'
    cpp.write_text(source)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/hal'), '-I', str(ROOT / 'lib/FsHelpers'),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'), str(cpp), '-o', str(path/'check')], check=True)
    subprocess.run([str(path/'check')], check=True)
