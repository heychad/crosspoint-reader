#pragma once
#include <Arduino.h>
#define HalStorage BaseHalStorage
#define Storage BaseStorage
#include "../../book_reading_stats_atomic/stubs/HalStorage.h"
#undef Storage
#undef HalStorage
class HalStorage : public BaseHalStorage {
 public:
  std::string failWritePath;
  HalFile openChecked(const char* path, int flags) {
    if ((flags & O_CREAT) && path == failWritePath) {
      failWritePath.clear();
      return {};
    }
    return BaseHalStorage::openChecked(path, flags);
  }
  bool openFileForWrite(const char* module, const std::string& path, HalFile& file) {
    if (path == failWritePath) {
      failWritePath.clear();
      return false;
    }
    return BaseHalStorage::openFileForWrite(module, path, file);
  }
  String readFile(const char* path) {
    std::string out;
    readFileToString("TEST", path, 16384, out);
    return String(out.c_str());
  }
  bool readFileToString(const char* module, const std::string& path, size_t cap, std::string& out) {
    HalFile file;
    if (!openFileForRead(module, path, file) || file.fileSize() == 0 || file.fileSize() > cap) return false;
    out.resize(file.fileSize());
    return file.read(out.data(), out.size()) == static_cast<int>(out.size());
  }
  bool writeFile(const char* path, const String& input) {
    HalFile file;
    return openFileForWrite("TEST", path, file) && file.write(input.data(), input.size()) == input.size();
  }
};
inline HalStorage Storage;
