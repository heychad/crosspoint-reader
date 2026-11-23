"""Exercise the production DAV COPY handler with source and publication faults."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/network/WebDAVHandler.cpp").read_text()
start = source.index("namespace {\nbool stageDavCopy(")
end = source.index("\n}\n", source.index("void WebDAVHandler::handleCopy(")) + 2
program = r"""
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <map>
#include <string>
#include <vector>
#include "network/ProtectedPaths.h"

#define LOG_DBG(...)
#define LOG_ERR(...)
void resetTaskWatchdogIfSubscribed() {}
uint32_t esp_random() { return 7; }

struct String : std::string {
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  bool isEmpty() const { return empty(); }
  int lastIndexOf(char c) const { return static_cast<int>(rfind(c)); }
  String substring(size_t start, size_t end) const { return substr(start, end - start); }
  friend String operator+(String prefix, const char* suffix) { prefix.append(suffix); return prefix; }
};
constexpr char SOURCE[] = "/old.epub";
constexpr char DEST[] = "/Books/new.epub";
constexpr char PART[] = "/Books/.dav-copy-0000000700000007.part";
constexpr char BACKUP[] = "/Books/.dav-copy-0000000700000007.bak";
enum class Fault { None, ReadError, ShortRead, EarlyEof, ShortWrite, Sync, Close, Publish, Rollback, Confirm, LateCreate };
Fault problem;
struct Entry {
  std::vector<uint8_t> bytes;
  bool directory = false;
};
struct HalFile {
  Entry* entry;
  bool writer = false;
  size_t offset = 0;
  explicit operator bool() const { return entry; }
  bool isDirectory() const { return entry->directory; }
  uint64_t fileSize64() const { return entry->bytes.size(); }
  bool hasError() const { return false; }
  int read(uint8_t* out, size_t count) {
    if (offset > 0 && problem == Fault::ReadError) return -1;
    if (offset > 0 && problem == Fault::EarlyEof) return 0;
    if (offset > 0 && problem == Fault::ShortRead) --count;
    std::copy_n(entry->bytes.begin() + offset, count, out);
    offset += count;
    return static_cast<int>(count);
  }
  size_t write(const uint8_t* data, size_t count) {
    if (problem == Fault::ShortWrite) return count - 1;
    entry->bytes.insert(entry->bytes.end(), data, data + count);
    return count;
  }
  bool sync() { return problem != Fault::Sync; }
  bool close() { return !writer || problem != Fault::Close; }
};
struct HalStorage {
  enum class PathStatus { Present, Missing, Failed };
  std::map<std::string, Entry> entries;
  bool locked = false, published = false;
  bool exists(const char* path) const { return entries.contains(path); }
  PathStatus lookupPath(const char* path) const {
    if (problem == Fault::Confirm && published) return PathStatus::Failed;
    return exists(path) ? PathStatus::Present : PathStatus::Missing;
  }
  HalFile openChecked(const char* path, int flags = O_RDONLY) {
    if (!flags) return {exists(path) ? &entries.at(path) : nullptr};
    assert(flags == (O_WRONLY | O_CREAT | O_EXCL));
    if (exists(path)) return {nullptr};
    return {&entries[path], true};
  }
  bool rename(const std::string& from, const std::string& to) {
    assert(locked);
    if ((from == PART && (problem == Fault::Publish || problem == Fault::Rollback)) ||
        (from == BACKUP && problem == Fault::Rollback)) return false;
    if (!entries.contains(from) || entries.contains(to)) return false;
    auto entry = entries.extract(from);
    entry.key() = to;
    entries.insert(std::move(entry));
    if (from == PART) published = true;
    return true;
  }
  bool remove(const char* path) { return entries.erase(path) == 1; }
  bool withStorageLock(bool (*operation)(void*), void* context) {
    if (problem == Fault::LateCreate) entries[DEST] = {{1, 2, 3}};
    locked = true;
    const bool result = operation(context);
    locked = false;
    return result;
  }
} Storage;
struct WebServer {
  int code = 0;
  void send(int value, const char* = "", const char* = "") { code = value; }
};
struct WebDAVHandler {
  String source = SOURCE, destination = DEST;
  bool overwrite = true;
  String getRequestPath(WebServer&) { return source; }
  String getDestinationPath(WebServer&) { return destination; }
  bool getOverwrite(WebServer&) { return overwrite; }
  bool isProtectedPath(const String&) { return false; }
  void handleCopy(WebServer&);
};
""" + source[start:end] + r"""
int main() {
  const std::vector<uint8_t> book(8193, 42), original = {1, 2, 3};
  const auto reset = [&](Fault next, bool existing = true) {
    Storage = {};
    problem = next;
    Storage.entries = {{"/", {{}, true}}, {"/Books", {{}, true}}, {SOURCE, {book}}};
    if (existing) Storage.entries[DEST] = {original};
  };
  WebDAVHandler dav;
  WebServer response;
  for (auto fault : {Fault::ReadError, Fault::ShortRead, Fault::EarlyEof,
                     Fault::ShortWrite, Fault::Sync, Fault::Close, Fault::Publish}) {
    reset(fault);
    dav.handleCopy(response);
    assert(response.code == 500 && Storage.entries.at(DEST).bytes == original);
    assert(Storage.entries.at(SOURCE).bytes == book && !Storage.exists(PART) && !Storage.exists(BACKUP));
  }
  reset(Fault::Rollback);
  dav.handleCopy(response);
  assert(response.code == 500 && !Storage.exists(DEST));
  assert(Storage.entries.at(BACKUP).bytes == original && Storage.entries.at(PART).bytes == book);
  reset(Fault::Confirm);
  dav.handleCopy(response);
  assert(response.code == 500 && Storage.entries.at(BACKUP).bytes == original);
  for (const bool existing : {false, true}) {
    reset(Fault::None, existing);
    dav.handleCopy(response);
    assert(response.code == (existing ? 204 : 201) && Storage.entries.at(DEST).bytes == book);
    assert(!Storage.exists(PART) && !Storage.exists(BACKUP));
  }
  reset(Fault::None);
  Storage.entries[PART] = {original};
  dav.handleCopy(response);
  assert(response.code == 500 && Storage.entries.at(PART).bytes == original);
  assert(Storage.entries.at(DEST).bytes == original);
  reset(Fault::None);
  dav.overwrite = false;
  dav.handleCopy(response);
  assert(response.code == 412 && Storage.entries.at(DEST).bytes == original);
  reset(Fault::LateCreate, false);
  dav.handleCopy(response);
  assert(response.code == 412 && Storage.entries.at(DEST).bytes == original && !Storage.exists(PART));
  reset(Fault::None);
  dav.overwrite = true;
  dav.destination = "/empty.epub";
  Storage.entries.at(SOURCE).bytes.clear();
  dav.handleCopy(response);
  assert(response.code == 201 && Storage.entries.at("/empty.epub").bytes.empty());
}
"""

with tempfile.TemporaryDirectory() as temporary:
    cpp = Path(temporary) / "copy.cpp"
    binary = Path(temporary) / "copy"
    cpp.write_text(program)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-DFREEINK_DEVICE_X4PRO=1",
                    *shlex.split(os.environ.get("CXXFLAGS", "")), "-I", str(ROOT / "src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
