#include "PersonalRelease.h"

#include <ArduinoJson.h>
#include <Memory.h>
#include <SettingsJsonAllocator.h>
#include <StreamingJsonParser.h>

#include <cstdio>
#include <cstring>
#include <span>
#include <string_view>

#include "PersonalUpdateVersion.h"

namespace {
bool whitespace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

// ArduinoJson accepts single quotes and unquoted keys; release metadata uses JSON syntax only.
class JsonSyntax {
  const char* cursor;
  const char* end;
  void spaces() {
    while (cursor != end && whitespace(*cursor)) ++cursor;
  }
  bool take(char c) {
    if (cursor == end || *cursor != c) return false;
    ++cursor;
    return true;
  }
  bool digits() {
    const char* start = cursor;
    while (cursor != end && *cursor >= '0' && *cursor <= '9') ++cursor;
    return cursor != start;
  }
  bool number() {
    take('-');
    if (take('0')) {
      if (cursor != end && *cursor >= '0' && *cursor <= '9') return false;
    } else if (!digits())
      return false;
    if (take('.') && !digits()) return false;
    if (take('e') || take('E')) {
      if (!take('+')) take('-');
      if (!digits()) return false;
    }
    return true;
  }
  bool literal(std::string_view text) {
    if (static_cast<size_t>(end - cursor) < text.size() || std::memcmp(cursor, text.data(), text.size())) return false;
    cursor += text.size();
    return true;
  }
  bool string() {
    if (!take('"')) return false;
    while (cursor != end) {
      const auto c = static_cast<uint8_t>(*cursor++);
      if (c == '"') return true;
      if (c < 0x20) return false;
      if (c == '\\') {
        if (cursor == end) return false;
        const char escaped = *cursor++;
        if (escaped == 'u') {
          for (int i = 0; i < 4; ++i) {
            if (cursor == end) return false;
            const char digit = *cursor++;
            if (!((digit >= '0' && digit <= '9') || (digit >= 'a' && digit <= 'f') || (digit >= 'A' && digit <= 'F')))
              return false;
          }
        } else if (std::string_view("\"\\/bfnrt").find(escaped) == std::string_view::npos)
          return false;
      } else if (c >= 0x80) {
        const int continuation = c >= 0xc2 && c <= 0xdf   ? 1
                                 : c >= 0xe0 && c <= 0xef ? 2
                                 : c >= 0xf0 && c <= 0xf4 ? 3
                                                          : -1;
        if (continuation < 0) return false;
        uint32_t codepoint = c & (continuation == 1 ? 0x1f : continuation == 2 ? 0x0f : 0x07);
        for (int i = 0; i < continuation; ++i) {
          if (cursor == end || (static_cast<uint8_t>(*cursor) & 0xc0) != 0x80) return false;
          codepoint = (codepoint << 6) | (static_cast<uint8_t>(*cursor++) & 0x3f);
        }
        const uint32_t minimum = continuation == 1 ? 0x80 : continuation == 2 ? 0x800 : 0x10000;
        if (codepoint < minimum || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
      }
    }
    return false;
  }
  bool value(unsigned depth) {
    spaces();
    if (cursor == end) return false;
    if (*cursor == '{' || *cursor == '[') {
      if (depth == 16) return false;
      const bool object = *cursor++ == '{';
      const char close = object ? '}' : ']';
      spaces();
      if (take(close)) return true;
      do {
        spaces();
        if (object) {
          if (!string()) return false;
          spaces();
          if (!take(':')) return false;
        }
        if (!value(depth + 1)) return false;
        spaces();
        if (take(close)) return true;
      } while (take(','));
      return false;
    }
    if (*cursor == '"') return string();
    if (*cursor == 't') return literal("true");
    if (*cursor == 'f') return literal("false");
    if (*cursor == 'n') return literal("null");
    return number();
  }

 public:
  JsonSyntax(const char* data, size_t size) : cursor(data), end(data + size) {}
  bool valid() {
    spaces();
    if (cursor == end || *cursor != '{' || !value(0)) return false;
    spaces();
    return cursor == end;
  }
};

struct KeyAudit {
  enum class Location { Root, Assets, Asset };
  StreamingJsonParser parser;
  uint8_t depth = 0, rootFields = 0, assetFields = 0;
  Location location = Location::Root;
  bool pendingAssets = false, closed = false, invalid = false;

  KeyAudit()
      : parser(JsonCallbacks{this, key, nullptr, nullptr, nullptr, nullptr, objectStart, objectEnd, arrayStart,
                             arrayEnd}) {}

  static void mark(uint8_t& fields, std::string_view name, std::span<const std::string_view> names, bool& bad) {
    for (size_t i = 0; i < names.size(); ++i) {
      if (names[i] != name) continue;
      const uint8_t bit = 1U << i;
      if (fields & bit) bad = true;
      fields |= bit;
      return;
    }
  }
  static void key(void* context, const char* text, size_t length) {
    auto& self = *static_cast<KeyAudit*>(context);
    const std::string_view name(text, length);
    self.pendingAssets = self.depth == 1 && name == "assets";
    static constexpr std::string_view ROOT[] = {"tag_name", "draft", "prerelease", "assets"};
    static constexpr std::string_view ASSET[] = {"name", "browser_download_url", "size", "digest", "state"};
    if (self.depth == 1) mark(self.rootFields, name, ROOT, self.invalid);
    if (self.depth == 3 && self.location == Location::Asset) mark(self.assetFields, name, ASSET, self.invalid);
  }
  static void objectStart(void* context) {
    auto& self = *static_cast<KeyAudit*>(context);
    if (++self.depth == 3 && self.location == Location::Assets) {
      self.location = Location::Asset;
      self.assetFields = 0;
    }
  }
  static void objectEnd(void* context) {
    auto& self = *static_cast<KeyAudit*>(context);
    if (self.depth == 3 && self.location == Location::Asset) self.location = Location::Assets;
    if (self.depth == 0)
      self.invalid = true;
    else if (--self.depth == 0)
      self.closed = true;
  }
  static void arrayStart(void* context) {
    auto& self = *static_cast<KeyAudit*>(context);
    if (++self.depth == 2 && self.pendingAssets) self.location = Location::Assets;
    self.pendingAssets = false;
  }
  static void arrayEnd(void* context) {
    auto& self = *static_cast<KeyAudit*>(context);
    if (self.depth == 2 && self.location == Location::Assets) self.location = Location::Root;
    if (self.depth == 0)
      self.invalid = true;
    else
      --self.depth;
  }
  bool check(const char* data, size_t size) {
    bool started = false;
    for (size_t i = 0; i < size; ++i) {
      if (closed) {
        if (!whitespace(data[i])) return false;
        continue;
      }
      if (!started && !whitespace(data[i])) {
        if (data[i] != '{') return false;
        started = true;
      }
      parser.feed(data + i, 1);
      if (invalid || parser.hasError()) return false;
    }
    return started && closed;
  }
};

std::string_view text(ArduinoJson::JsonVariantConst value) {
  if (!value.is<const char*>()) return {};
  const auto string = value.as<ArduinoJson::JsonString>();
  return {string.c_str(), string.size()};
}

int hex(const char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}
}  // namespace

personalupdate::ReleaseResult personalupdate::parseRelease(char* json, const size_t length, const size_t maxImageBytes,
                                                           Release& output) {
  output = {};
  const auto fail = [&](const ReleaseResult result) {
    output = {};
    return result;
  };
  if (!json || !length || length > MAX_RELEASE_BYTES || std::memchr(json, '\0', length)) return ReleaseResult::Invalid;
  if (!JsonSyntax(json, length).valid()) return ReleaseResult::Invalid;
  {
    auto audit = makeUniqueNoThrow<KeyAudit>();
    if (!audit) return ReleaseResult::NoMemory;
    if (!audit->check(json, length)) return ReleaseResult::Invalid;
  }
  settingsrecovery::SettingsJsonAllocator allocator;
  ArduinoJson::JsonDocument filter(&allocator);
  filter["tag_name"] = true;
  filter["draft"] = true;
  filter["prerelease"] = true;
  for (const char* field : {"name", "browser_download_url", "size", "digest", "state"})
    filter["assets"][0][field] = true;
  if (filter.overflowed()) return ReleaseResult::NoMemory;
  ArduinoJson::JsonDocument document(&allocator);
  const auto error = deserializeJson(document, json, length, ArduinoJson::DeserializationOption::Filter(filter),
                                     ArduinoJson::DeserializationOption::NestingLimit(16));
  if (error)
    return error == ArduinoJson::DeserializationError::NoMemory ? ReleaseResult::NoMemory : ReleaseResult::Invalid;
  if (!document.is<ArduinoJson::JsonObject>() || !document["draft"].is<bool>() || document["draft"].as<bool>() ||
      !document["prerelease"].is<bool>() || document["prerelease"].as<bool>() ||
      !document["assets"].is<ArduinoJson::JsonArray>())
    return ReleaseResult::Invalid;
  const auto version = text(document["tag_name"]);
  if (!isReleaseTag(version) || version.size() >= sizeof(output.version)) return ReleaseResult::Invalid;
  std::memcpy(output.version, version.data(), version.size());
  char filename[48]{};
  const int nameLength = std::snprintf(filename, sizeof(filename), "crosspoint-%s-x4pro.bin", output.version);
  const int urlLength =
      std::snprintf(output.url, sizeof(output.url),
                    "https://github.com/heychad/crosspoint-reader/releases/download/%s/%s", output.version, filename);
  if (nameLength <= 0 || static_cast<size_t>(nameLength) >= sizeof(filename) || urlLength <= 0 ||
      static_cast<size_t>(urlLength) >= sizeof(output.url))
    return fail(ReleaseResult::Invalid);
  const auto assets = document["assets"].as<ArduinoJson::JsonArrayConst>();
  if (assets.size() > 16) return fail(ReleaseResult::Invalid);
  bool found = false;
  for (const auto value : assets) {
    if (!value.is<ArduinoJson::JsonObjectConst>()) return fail(ReleaseResult::Invalid);
    if (text(value["name"]) != filename) continue;
    if (found || text(value["state"]) != "uploaded" || text(value["browser_download_url"]) != output.url ||
        !value["size"].is<uint32_t>())
      return fail(ReleaseResult::Invalid);
    output.size = value["size"].as<uint32_t>();
    if (output.size < 24 || output.size > maxImageBytes) return fail(ReleaseResult::Invalid);
    const auto digest = text(value["digest"]);
    if (digest.size() != 71 || !digest.starts_with("sha256:")) return fail(ReleaseResult::Invalid);
    for (size_t i = 0; i < output.sha256.size(); ++i) {
      const int high = hex(digest[7 + 2 * i]), low = hex(digest[8 + 2 * i]);
      if (high < 0 || low < 0) return fail(ReleaseResult::Invalid);
      output.sha256[i] = static_cast<uint8_t>((high << 4) | low);
    }
    found = true;
  }
  return found ? ReleaseResult::Valid : fail(ReleaseResult::MissingAsset);
}
