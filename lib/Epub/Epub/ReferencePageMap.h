#pragma once

#include <Print.h>
#include <StreamingJsonParser.h>

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

// CrossInk x-locations v1 reference units. No layout or progress cache is written.
class ReferencePageMap {
 public:
  static constexpr size_t MAX_SPINE_ENTRIES = 1024;
  static constexpr size_t MAX_MANIFEST_BYTES = 64 * 1024;

  struct Target {
    int spineIndex;
    uint32_t unitOffset;
    uint32_t unitCount;
    bool characters;
  };
  struct Position {
    int spineIndex;
    float fraction;
  };

  void clear();
  bool resolve(int spineIndex, float spineProgress, uint32_t& page, uint32_t& pages) const;
  uint32_t pageCount() const { return valid ? pages : 0; }
  std::optional<Target> target(uint32_t page) const;
  std::optional<Position> targetPercent(uint8_t percent) const;

 private:
  friend class ReferencePageParser;
  struct Range {
    uint32_t wordStart = 0;
    uint32_t wordCount = 0;
    uint32_t characterStart = 0;
    uint32_t characterCount = 0;
  };
  std::unique_ptr<Range[]> ranges;
  size_t spineCount = 0;
  uint32_t totalUnits = 0;
  uint32_t unitsPerPage = 0;
  uint32_t pages = 0;
  bool characters = false;
  bool valid = false;
  std::optional<Target> targetUnit(uint32_t unit) const;
};

// Owned only while reading the optional manifest. The SDK token buffer stays on heap.
class ReferencePageParser final : public Print {
 public:
  ReferencePageParser(ReferencePageMap& destination, size_t spineCount);
  bool ready() const;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t* data, size_t length) override;
  bool finish();

 private:
  enum class Key : uint8_t {
    None,
    Format,
    Version,
    TotalLocations,
    TotalWords,
    TotalCharacters,
    WordsPerPage,
    CharactersPerPage,
    TotalPages,
    Spine,
    Index,
    WordStart,
    WordCount,
    CharacterStart,
    CharacterCount
  };
  enum class Container : uint8_t { Object, Array };
  static void onKey(void* context, const char* value, size_t length);
  static void onString(void* context, const char* value, size_t length);
  static void onNumber(void* context, const char* value, size_t length);
  static void onBool(void* context, bool value);
  static void onNull(void* context);
  static void onObjectStart(void* context);
  static void onObjectEnd(void* context);
  static void onArrayStart(void* context);
  static void onArrayEnd(void* context);
  void start(Container container);
  void end(Container container);
  void scalar();
  void commitRange();

  ReferencePageMap& destination;
  StreamingJsonParser parser;
  std::array<Container, StreamingJsonParser::MAX_NESTING> containers{};
  std::bitset<ReferencePageMap::MAX_SPINE_ENTRIES> seenEntries;
  ReferencePageMap::Range range;
  size_t bytesRead = 0;
  size_t depth = 0;
  size_t ordinal = 0;
  uint32_t index = 0;
  uint32_t totalWords = 0;
  uint32_t totalCharacters = 0;
  uint32_t wordsPerPage = 0;
  uint32_t charactersPerPage = 0;
  uint32_t totalLocations = 0;
  uint32_t version = 0;
  uint32_t rootKeys = 0;
  uint32_t entryKeys = 0;
  Key key = Key::None;
  bool formatValid = false;
  bool spineOpen = false;
  bool entryOpen = false;
  bool rootClosed = false;
  bool failed = false;
};
