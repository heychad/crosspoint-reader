"""Exercise the HAL checked-lookup method with directory I/O failure injection."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "lib/hal/HalStorage.cpp").read_text()
start = source.index("HalStorage::PathStatus HalStorage::lookupPath(")
end = source.index("\nbool HalStorage::withStorageLock(", start)
method = source[start:end]
harness = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include "CheckedStoragePath.h"
constexpr int O_RDONLY = 0;
std::map<std::string, bool> entries;
std::set<std::string> lookupFailures;
std::set<std::string> closeFailures;
bool mounted = true;
bool rootFailure = false;
bool allocationFailure = false;
bool strictSupported = true;
unsigned strictCalls = 0;
template<class T> std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  if (allocationFailure) return {};
  return std::unique_ptr<T>(new std::remove_extent_t<T>[count]());
}
struct FsFile {
  std::string path;
  bool opened = false;
  bool directory = false;
  int error = 0;
  explicit operator bool() const { return opened; }
  bool isDirectory() const { return directory; }
  bool enableStrictLookup() { ++strictCalls; return strictSupported; }
  int getError() const { assert(opened); return error; }
  bool open(FsFile* parent, const char* component, int) {
    assert(parent->opened && parent->directory);
    const auto candidate = parent->path == "/" ? parent->path + component : parent->path + "/" + component;
    if (lookupFailures.contains(candidate)) {
      parent->error = 1;
      return false;
    }
    const auto found = entries.find(candidate);
    if (found == entries.end()) return false;
    path = candidate;
    opened = true;
    directory = found->second;
    return true;
  }
  bool close() {
    assert(opened);
    opened = false;
    return !closeFailures.contains(path);
  }
};
struct Manager {
  bool ready() const { return mounted; }
  FsFile open(const char* path) {
    assert(std::string(path) == "/");
    return FsFile{"/", !rootFailure, true, 0};
  }
} SDCard;
struct HalStorage {
  struct StorageLock {};
  enum class PathStatus {Missing, Present, Failed};
  PathStatus lookupPath(const char*);
};
''' + method + r'''
int main() {
  using Status = HalStorage::PathStatus;
  HalStorage storage;
  const char* path = "/.crosspoint/bookmarks/epub_123.json";
  entries = {{"/.crosspoint", true}, {"/.crosspoint/bookmarks", true}, {path, false}};
  assert(storage.lookupPath(path) == Status::Present);
  assert(strictCalls == 3);
  assert(storage.lookupPath("/.crosspoint/bookmarks/absent.json") == Status::Missing);
  for (const auto& component : {"/.crosspoint", "/.crosspoint/bookmarks", path}) {
    lookupFailures.insert(component);
    assert(storage.lookupPath(path) == Status::Failed);
    lookupFailures.clear();
  }
  for (const auto& component : {"/", "/.crosspoint", "/.crosspoint/bookmarks", path}) {
    closeFailures.insert(component);
    assert(storage.lookupPath(path) == Status::Failed);
    closeFailures.clear();
  }
  rootFailure = true;
  assert(storage.lookupPath(path) == Status::Failed);
  rootFailure = false;
  mounted = false;
  assert(storage.lookupPath(path) == Status::Failed);
  mounted = true;
  strictSupported = false;
  assert(storage.lookupPath(path) == Status::Failed);
  strictSupported = true;
  for (const char* invalid : {static_cast<const char*>(nullptr), "relative", "//bad", "/../bad", "/.crosspoint/"})
    assert(storage.lookupPath(invalid) == Status::Failed);
  for (const char* invalid : {"/missing/bad:name", "/missing/../target", "/missing/", "/missing//file"})
    assert(storage.lookupPath(invalid) == Status::Failed);
  for (const std::string invalid : {std::string("/.crosspoint/bad:name"), std::string("/.crosspoint/ "),
       std::string("/.crosspoint/trailing."), std::string("/.crosspoint/") + std::string(256, 'x'),
       std::string("/.crosspoint/bad\xc3"), std::string("/.crosspoint/overlong\xc0\xaf"),
       std::string("/.crosspoint/surrogate\xed\xa0\x80")})
    assert(storage.lookupPath(invalid.c_str()) == Status::Failed);
  for (const std::string name : {std::string("My Book (edition 2).json"), std::string("Caf\xc3\xa9_\xe4\xb8\x96\xe7\x95\x8c.json"), std::string(180, 'x') + ".json.bak"}) {
    const auto full = std::string("/.crosspoint/bookmarks/") + name;
    entries[full] = false;
    assert(storage.lookupPath(full.c_str()) == Status::Present);
  }
  allocationFailure = true;
  assert(storage.lookupPath(path) == Status::Failed);
  allocationFailure = false;
  assert(storage.lookupPath("/") == Status::Present);
  entries["/.crosspoint"] = false;
  assert(storage.lookupPath(path) == Status::Failed);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "lookup.cpp"
    executable = Path(directory) / "lookup"
    cpp.write_text(harness)
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++20",
        *shlex.split(os.environ.get("CXXFLAGS", "")), "-I", str(ROOT / "lib/hal"), str(cpp), "-o", str(executable)
    ], check=True)
    subprocess.run([str(executable)], check=True)
print("HAL lookup distinguishes missing metadata paths from directory I/O errors")
