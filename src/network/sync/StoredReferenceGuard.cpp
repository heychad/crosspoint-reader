#include "StoredReferenceGuard.h"

#include <ArduinoJson.h>
#include <Memory.h>
#include <SettingsJsonAllocator.h>
#include <common/FsUtf.h>
#include <common/upcase.h>
#include <strings.h>

#include <algorithm>
#include <cstring>

#include "PublishIntent.h"

namespace syncpublish {
namespace {
constexpr size_t MAX_BYTES = 65536;
constexpr size_t CHUNK_BYTES = 512;
struct LexLevel {
  char kind = 0;
  bool key = false;
  bool path = false;
  bool books = false;
  bool open = false;
};
// Reject duplicate relevant keys and escaped object keys; ArduinoJson normally
// retains only the last duplicate. JSON syntax is validated separately.
struct KeyAudit {
  LexLevel levels[16]{};
  size_t depth = 0;
  bool string = false, escape = false, key = false, invalid = false, tooLong = false, rootDone = false;
  char text[32]{};
  size_t length = 0;
  void consume(char c) {
    if (invalid) return;
    if (rootDone && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
      invalid = true;
      return;
    }
    if (string) {
      if (escape) {
        escape = false;
        if (key) invalid = true;
        return;
      }
      if (c == '\\') {
        escape = true;
        return;
      }
      if (c != '"') {
        if (key) {
          if (length < sizeof(text))
            text[length++] = c;
          else
            tooLong = true;
        }
        return;
      }
      string = false;
      if (key && depth) {
        auto& level = levels[depth - 1];
        const auto matches = [&](const char* name) {
          return !tooLong && strlen(name) == length && memcmp(text, name, length) == 0;
        };
        if (matches("path")) {
          if (level.path) invalid = true;
          level.path = true;
        }
        if (depth == 1 && matches("books")) {
          if (level.books) invalid = true;
          level.books = true;
        }
        if (depth == 1 && matches("openEpubPath")) {
          if (level.open) invalid = true;
          level.open = true;
        }
      }
      return;
    }
    if (c == '"') {
      string = true;
      escape = false;
      length = 0;
      tooLong = false;
      key = depth && levels[depth - 1].kind == '{' && levels[depth - 1].key;
      if (key) levels[depth - 1].key = false;
    } else if (c == '{' || c == '[') {
      if (depth == 16) {
        invalid = true;
        return;
      }
      levels[depth++] = LexLevel{c, c == '{'};
    } else if (c == '}' || c == ']') {
      if (!depth) {
        invalid = true;
        return;
      }
      --depth;
      if (!depth) rootDone = true;
    } else if (c == ',' && depth && levels[depth - 1].kind == '{') {
      levels[depth - 1].key = true;
    }
  }
};
struct TrackedReader {
  HalStorage& storage;
  const HalStorage::MutationSnapshot& snapshot;
  HalFile& file;
  syncstage::DigestTick tick;
  void* context;
  uint64_t remaining;
  char buffer[CHUNK_BYTES]{};
  size_t position = 0, available = 0;
  ReferenceGuardResult failure = ReferenceGuardResult::Clear;
  KeyAudit audit;
  int read() {
    if (position == available) {
      if (!remaining) return -1;
      if (!tick(context)) {
        failure = ReferenceGuardResult::Cancelled;
        return -1;
      }
      if (!storage.mutationSnapshotUnchanged(snapshot)) {
        failure = ReferenceGuardResult::Changed;
        return -1;
      }
      const size_t count = static_cast<size_t>(std::min<uint64_t>(remaining, CHUNK_BYTES));
      if (file.read(buffer, count) != static_cast<int>(count) || file.hasError()) {
        failure = ReferenceGuardResult::Unavailable;
        return -1;
      }
      if (!storage.mutationSnapshotUnchanged(snapshot)) {
        failure = ReferenceGuardResult::Changed;
        return -1;
      }
      remaining -= count;
      position = 0;
      available = count;
    }
    const auto value = static_cast<unsigned char>(buffer[position++]);
    audit.consume(static_cast<char>(value));
    return value;
  }
  size_t readBytes(char* out, size_t length) {
    size_t count = 0;
    while (count < length) {
      const int c = read();
      if (c < 0) break;
      out[count++] = static_cast<char>(c);
    }
    return count;
  }
};
}  // namespace
namespace {
bool validReference(std::string_view reference) {
  if (reference.size() < 2 || reference.size() > 4096 || reference.front() != '/' || reference.back() == '/' ||
      reference.find('\0') != reference.npos || reference.find('\\') != reference.npos)
    return false;
  size_t start = 1;
  while (start < reference.size()) {
    const auto end = reference.find('/', start);
    const auto component = reference.substr(start, end == reference.npos ? reference.size() - start : end - start);
    if (component.empty() || component.size() > 765) return false;
    if (!validCheckedStorageComponent(component)) return false;
    for (unsigned char c : component)
      if (c < 0x20 || c == 0x7f) return false;
    if (end == reference.npos) break;
    start = end + 1;
  }
  for (size_t i = 0; i + 1 < reference.size(); ++i)
    if (reference[i] == '~' && reference[i + 1] >= '0' && reference[i + 1] <= '9') return false;
  return true;
}
// Compare the same UTF-16 units and upcase table used by SdFat lookups.
bool fatEqual(std::string_view left, std::string_view right) {
  struct Units {
    const char* next;
    const char* end;
    uint16_t pending = 0;
    bool read(uint16_t& unit) {
      if (pending) {
        unit = pending;
        pending = 0;
        return true;
      }
      if (next == end) return false;
      uint16_t low = 0;
      const char* after = FsUtf::mbToU16(next, end, &unit, &low);
      if (!after) return false;
      next = after;
      pending = low;
      return true;
    }
    bool done() const { return next == end && !pending; }
  } a{left.data(), left.data() + left.size()}, b{right.data(), right.data() + right.size()};
  while (!a.done() && !b.done()) {
    uint16_t x, y;
    if (!a.read(x) || !b.read(y)) return true;
    if (toUpcase(x) != toUpcase(y)) return false;
  }
  return a.done() && b.done();
}
}  // namespace
bool referenceMayConflict(std::string_view reference, std::string_view destination) {
  if (reference.empty()) return false;
  if (!validReference(reference)) return true;
  const auto leaf = reference.substr(reference.find_last_of('/') + 1);
  const auto target = destination.substr(destination.find_last_of('/') + 1);
  if (!validCheckedStorageComponent(target)) return true;
  return fatEqual(leaf, target);
}
bool referenceMayReferTo(std::string_view reference, std::string_view source) {
  if (reference.empty()) return false;
  if (!validReference(reference) || !validReference(source)) return true;
  return fatEqual(reference, source);
}
namespace {
ReferenceGuardResult inspectFile(HalStorage& storage, const HalStorage::MutationSnapshot& snapshot, const char* path,
                                 bool recent, std::string_view destination, syncstage::DigestTick tick, void* context,
                                 bool wholePath) {
  if (!tick(context)) return ReferenceGuardResult::Cancelled;
  const auto status = storage.lookupPath(path);
  if (!storage.mutationSnapshotUnchanged(snapshot)) return ReferenceGuardResult::Changed;
  if (status == HalStorage::PathStatus::Missing) return ReferenceGuardResult::Clear;
  if (status != HalStorage::PathStatus::Present) return ReferenceGuardResult::Unavailable;
  auto file = storage.openChecked(path, O_RDONLY);
  if (!file) return ReferenceGuardResult::Unavailable;
  const bool directory = file.isDirectory();
  const uint64_t size = file.fileSize64();
  if (directory || file.hasError() || !size || size > MAX_BYTES) {
    file.close();
    return ReferenceGuardResult::Unavailable;
  }
  auto reader = makeUniqueNoThrow<TrackedReader>(storage, snapshot, file, tick, context, size);
  if (!reader) {
    file.close();
    return ReferenceGuardResult::Unavailable;
  }
  settingsrecovery::SettingsJsonAllocator allocator;
  JsonDocument filter(&allocator);
  if (recent)
    filter["books"][0]["path"] = true;
  else
    filter["openEpubPath"] = true;
  JsonDocument doc(&allocator);
  const auto error =
      deserializeJson(doc, *reader, DeserializationOption::Filter(filter), DeserializationOption::NestingLimit(16));
  bool trailing = false;
  for (int c = reader->read(); c >= 0; c = reader->read())
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n') trailing = true;
  const auto failure = reader->failure;
  const bool auditFailed = reader->audit.invalid || reader->audit.depth != 0 || reader->audit.string;
  const bool clean = !file.hasError();
  const bool closed = file.close();
  if (!storage.mutationSnapshotUnchanged(snapshot)) return ReferenceGuardResult::Changed;
  if (failure != ReferenceGuardResult::Clear) return failure;
  if (error || filter.overflowed() || doc.overflowed() || trailing || auditFailed || !clean || !closed ||
      !doc.is<JsonObjectConst>())
    return ReferenceGuardResult::Unavailable;
  const auto check = [&](JsonVariantConst value) {
    if (!value.is<const char*>()) return ReferenceGuardResult::Unavailable;
    const JsonString text = value.as<JsonString>();
    if (!text.c_str() || strlen(text.c_str()) != text.size()) return ReferenceGuardResult::Unavailable;
    const std::string_view reference(text.c_str(), text.size());
    const bool conflict =
        wholePath ? referenceMayReferTo(reference, destination) : referenceMayConflict(reference, destination);
    return conflict ? ReferenceGuardResult::Conflict : ReferenceGuardResult::Clear;
  };
  if (recent) {
    const auto books = doc["books"];
    if (books.isNull()) return ReferenceGuardResult::Clear;
    if (!books.is<JsonArrayConst>()) return ReferenceGuardResult::Unavailable;
    for (JsonVariantConst book : books.as<JsonArrayConst>()) {
      if (!book.is<JsonObjectConst>()) return ReferenceGuardResult::Unavailable;
      const auto result = check(book["path"]);
      if (result != ReferenceGuardResult::Clear) return result;
    }
  } else {
    const auto pathValue = doc["openEpubPath"];
    if (!pathValue.isNull()) return check(pathValue);
  }
  return ReferenceGuardResult::Clear;
}
ReferenceGuardResult inspectReferences(HalStorage& storage, std::string_view destination, syncstage::DigestTick tick,
                                       void* context, bool wholePath) {
  if (!(wholePath ? validReference(destination) : validDestination(destination)) || !tick)
    return ReferenceGuardResult::Invalid;
  HalStorage::MutationSnapshot snapshot;
  if (!storage.beginMutationSnapshot(snapshot)) return ReferenceGuardResult::Unavailable;
  struct Source {
    const char* path;
    bool recent;
  };
  constexpr Source sources[] = {
      {"/.crosspoint/state.json", false},           {"/.crosspoint/state.json.bak", false},
      {"/.crosspoint/state.json.tmp", false},       {"/.crosspoint/state.json.sleep-bak", false},
      {"/.crosspoint/state.json.sleep-tmp", false}, {"/.crosspoint/recent.json", true},
      {"/.crosspoint/recent.json.bak", true},       {"/.crosspoint/recent.json.tmp", true}};
  for (const auto& source : sources) {
    const auto result =
        inspectFile(storage, snapshot, source.path, source.recent, destination, tick, context, wholePath);
    if (result != ReferenceGuardResult::Clear) return result;
  }
  return storage.mutationSnapshotUnchanged(snapshot) ? ReferenceGuardResult::Clear : ReferenceGuardResult::Changed;
}
}  // namespace
ReferenceGuardResult inspectStoredReferences(HalStorage& storage, std::string_view destination,
                                             syncstage::DigestTick tick, void* context) {
  return inspectReferences(storage, destination, tick, context, false);
}
ReferenceGuardResult inspectStoredSourceReferences(HalStorage& storage, std::string_view source,
                                                   syncstage::DigestTick tick, void* context) {
  return inspectReferences(storage, source, tick, context, true);
}
}  // namespace syncpublish
