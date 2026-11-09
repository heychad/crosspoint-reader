#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cstdint>

class CrossPointSettings;

struct ReaderPreferences {
  static constexpr size_t FIELD_COUNT = 15;
  static constexpr uint32_t FONT_BIT = 1U << FIELD_COUNT;
  static constexpr uint32_t ALL_FIELDS = (FONT_BIT << 1U) - 1U;
  std::array<uint8_t, FIELD_COUNT> values{};
  char font[32]{};

  static ReaderPreferences capture(const CrossPointSettings& settings);
  void apply(CrossPointSettings& settings, uint32_t mask = ALL_FIELDS) const;
  uint32_t difference(const ReaderPreferences& other) const;
  void write(JsonObject object, uint32_t mask) const;
  bool read(JsonObjectConst object, uint32_t& mask);
};

struct BookReaderSettings {
  ReaderPreferences preferences;
  uint32_t mask = 0;
};

// Owns the temporary projection of book preferences onto the existing reader settings.
class BookReaderSettingsScope {
 public:
  bool active() const { return enabled; }
  void begin(CrossPointSettings& settings, const BookReaderSettings& book);
  BookReaderSettings capture(const CrossPointSettings& settings);
  void reset(CrossPointSettings& settings);
  void end(CrossPointSettings& settings);
  void beforeGlobalLoad(CrossPointSettings& settings);
  void afterGlobalLoad(CrossPointSettings& settings);
  void writeGlobal(JsonObject object) const;

 private:
  bool enabled = false;
  uint32_t overrideMask = 0;
  ReaderPreferences defaults;
  ReaderPreferences lastEffective;
};
