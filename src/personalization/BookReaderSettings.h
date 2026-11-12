#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>

class CrossPointSettings;

struct ReaderPreferences {
  static constexpr size_t FIELD_COUNT = 15;
  static constexpr uint32_t FONT_BIT = 1U << FIELD_COUNT;
  static constexpr uint32_t DICTIONARY_BIT = FONT_BIT << 1U;
  static constexpr uint32_t INDEXING_BIT = DICTIONARY_BIT << 1U;
  static constexpr uint32_t VERTICAL_MARGIN_BIT = INDEXING_BIT << 1U;
  static constexpr uint32_t ALL_FIELDS = (VERTICAL_MARGIN_BIT << 1U) - 1U;
  std::array<uint8_t, FIELD_COUNT> values{};
  char font[32]{};
  char dictionary[32]{};
  uint8_t indexingMethod = 0;
  uint8_t verticalMargin = 5;

  static ReaderPreferences capture(const CrossPointSettings& settings);
  void apply(CrossPointSettings& settings, uint32_t mask = ALL_FIELDS) const;
  uint32_t difference(const ReaderPreferences& other) const;
  void write(JsonObject object, uint32_t mask) const;
  bool read(JsonObjectConst object, uint32_t& mask);
};

struct BookReaderSettings {
  ReaderPreferences preferences;
  uint32_t mask = 0;
  bool safeMode = false;
};

// Owns the temporary projection of book preferences onto the existing reader settings.
class BookReaderSettingsScope {
 public:
  bool active() const { return enabled; }
  void begin(CrossPointSettings& settings, const BookReaderSettings& book);
  // Capture can restore Safe Mode's temporary values when a layout control changes.
  BookReaderSettings capture(CrossPointSettings& settings);
  void reset(CrossPointSettings& settings);
  // Null inherits the global choice; an empty string explicitly disables lookup.
  bool setDictionary(CrossPointSettings& settings, const char* name);
  void end(CrossPointSettings& settings);
  void beforeGlobalLoad(CrossPointSettings& settings);
  void afterGlobalLoad(CrossPointSettings& settings);
  void writeGlobal(JsonObject object) const;

 private:
  bool enabled = false;
  uint32_t overrideMask = 0;
  ReaderPreferences defaults;
  ReaderPreferences lastEffective;
  bool safeMode = false;
  std::array<uint8_t, 2> safeModeUnderlying{};
  void applySafeMode(CrossPointSettings& settings);
  void restoreSafeModeUnderlying(ReaderPreferences& preferences) const;
};
