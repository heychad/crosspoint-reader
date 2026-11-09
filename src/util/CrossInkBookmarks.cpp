#include "CrossInkBookmarks.h"

#include <CredentialIntegrity.h>
#include <Logging.h>
#include <Serialization.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>

namespace {
constexpr uint16_t MAX_BOOKMARKS = 1024;
constexpr uint32_t MAX_HEADER_STRING = 4096;

bool readHeaderString(HalFile& file, const std::string* expected = nullptr) {
  uint32_t length = 0;
  if (!serialization::tryReadPod(file, length) || length > MAX_HEADER_STRING ||
      length > static_cast<uint32_t>(std::max(0, file.available())))
    return false;
  if (!expected) return file.seek(file.position() + length);
  if (length != expected->size()) return false;
  char buffer[64];
  for (uint32_t offset = 0; offset < length;) {
    const size_t size = std::min<size_t>(sizeof(buffer), length - offset);
    if (file.read(buffer, size) != static_cast<int>(size) || memcmp(buffer, expected->data() + offset, size))
      return false;
    offset += size;
  }
  return true;
}
}  // namespace

bool CrossInkBookmarks::readHeader(Source& source, const std::string& bookPath) {
  auto& file = source.file;
  auto& version = source.version;
  if (!serialization::tryReadPod(file, version) || version < 2 || version > 5) return false;
  if (version == 2) {
    uint8_t count = 0;
    if (!serialization::tryReadPod(file, count)) return false;
    source.count = count;
  } else if (!serialization::tryReadPod(file, source.count)) {
    return false;
  }
  if (source.count > MAX_BOOKMARKS || !readHeaderString(file) || !readHeaderString(file) ||
      !readHeaderString(file, &bookPath))
    return false;
  source.recordsOffset = file.position();
  source.recordSize = 2 + 4 + 4 + 48 + (version >= 4 ? 2 : 0) + (version >= 5 ? 64 : 0);
  return static_cast<uint64_t>(source.recordsOffset) + static_cast<uint64_t>(source.recordSize) * source.count <=
         file.size();
}

bool CrossInkBookmarks::open(const std::string& bookPath) {
  close();
  invalidFile = false;
  if (!Storage.monitoredStorageHealthy()) {
    invalidFile = true;
    return false;
  }
  const uint32_t crc = credential_integrity::crc32(bookPath);
  const size_t hash = std::hash<std::string>{}(bookPath);
  const size_t keys[] = {crc, hash};
  char path[80];
  bool found = false;
  for (size_t i = 0; i < sources.size(); ++i) {
    if (!Storage.monitoredStorageHealthy()) {
      invalidFile = true;
      break;
    }
    if (i == 1 && keys[0] == keys[1]) continue;
    snprintf(path, sizeof(path), "/.crosspoint/bookmarks/epub_%zu.bin", keys[i]);
    const auto status = Storage.lookupPath(path);
    if (!Storage.monitoredStorageHealthy()) {
      invalidFile = true;
      break;
    }
    if (status == HalStorage::PathStatus::Failed) {
      invalidFile = true;
      continue;
    }
    if (status == HalStorage::PathStatus::Missing) continue;
    auto& source = sources[i];
    source.file = Storage.open(path);
    if (source.file && Storage.monitoredStorageHealthy() && readHeader(source, bookPath) &&
        Storage.monitoredStorageHealthy()) {
      entryCount += source.count;
      found = true;
      continue;
    }
    invalidFile = true;
    if (source.file) source.file.close();
    source.count = 0;
    LOG_ERR("BKM", "Invalid CrossInk bookmark file: %s", path);
    if (!Storage.monitoredStorageHealthy()) break;
  }
  return found;
}

void CrossInkBookmarks::close() {
  for (auto& source : sources) {
    if (source.file) source.file.close();
    source.count = 0;
  }
  entryCount = 0;
}

bool CrossInkBookmarks::read(uint16_t index, Entry& entry) {
  entry = {};
  if (index >= entryCount || !Storage.monitoredStorageHealthy()) return false;
  Source& source = index < sources[0].count ? sources[0] : sources[1];
  if (&source == &sources[1]) index -= sources[0].count;
  auto& file = source.file;
  const auto version = source.version;
  if (!file || !file.seek(source.recordsOffset + static_cast<uint32_t>(index) * source.recordSize)) return false;
  uint32_t timestamp = 0;
  if (!serialization::tryReadPod(file, entry.spine) || !serialization::tryReadPod(file, entry.progress) ||
      !serialization::tryReadPod(file, timestamp) ||
      file.read(entry.chapter, sizeof(entry.chapter)) != sizeof(entry.chapter))
    return false;
  if (version >= 4 && !serialization::tryReadPod(file, entry.paragraph)) return false;
  if (version >= 5 && file.read(entry.snippet, sizeof(entry.snippet)) != sizeof(entry.snippet)) return false;
  entry.chapter[sizeof(entry.chapter) - 1] = '\0';
  entry.snippet[sizeof(entry.snippet) - 1] = '\0';
  return Storage.monitoredStorageHealthy() && std::isfinite(entry.progress) && entry.progress >= 0 &&
         entry.progress <= 1;
}
