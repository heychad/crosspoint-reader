#include "BookReaderSettings.h"

#include <CrossPointSettings.h>

#include <cstring>

namespace {
struct Field {
  const char* key;
  uint8_t CrossPointSettings::* member;
  uint8_t minimum;
  uint8_t maximum;
};
constexpr std::array<Field, ReaderPreferences::FIELD_COUNT> FIELDS = {{
    {"fontSize", &CrossPointSettings::fontPointSize, 8, 255},
    {"lineHeight", &CrossPointSettings::lineSpacing, 0, 6},
    {"wordSpacing", &CrossPointSettings::wordSpacing, 50, 200},
    {"characterSpacing", &CrossPointSettings::characterSpacing, 0, 4},
    {"orientation", &CrossPointSettings::orientation, 0, 3},
    {"screenMargin", &CrossPointSettings::screenMargin, 5, 40},
    {"paragraphAlignment", &CrossPointSettings::paragraphAlignment, 0, 4},
    {"extraParagraphSpacing", &CrossPointSettings::extraParagraphSpacing, 0, 1},
    {"paragraphIndentSpaces", &CrossPointSettings::paragraphIndentSpaces, 0, 5},
    {"hyphenationEnabled", &CrossPointSettings::hyphenationEnabled, 0, 1},
    {"embeddedStyle", &CrossPointSettings::embeddedStyle, 0, 1},
    {"textAntiAliasing", &CrossPointSettings::textAntiAliasing, 0, 1},
    {"imageRendering", &CrossPointSettings::imageRendering, 0, 3},
    {"focusReadingEnabled", &CrossPointSettings::focusReadingEnabled, 0, 1},
    {"epubRenderMode", &CrossPointSettings::epubRenderMode, 0, EPUB_RENDER_MODE_COUNT - 1},
}};
}  // namespace

ReaderPreferences ReaderPreferences::capture(const CrossPointSettings& settings) {
  ReaderPreferences result;
  for (size_t i = 0; i < FIELDS.size(); ++i) result.values[i] = settings.*FIELDS[i].member;
  static_assert(sizeof(result.font) == sizeof(settings.sdFontFamilyName));
  memcpy(result.font, settings.sdFontFamilyName, sizeof(result.font));
  result.font[sizeof(result.font) - 1] = '\0';
  return result;
}

void ReaderPreferences::apply(CrossPointSettings& settings, uint32_t mask) const {
  for (size_t i = 0; i < FIELDS.size(); ++i)
    if (mask & (1U << i)) settings.*FIELDS[i].member = values[i];
  if (mask & FONT_BIT) memcpy(settings.sdFontFamilyName, font, sizeof(font));
}

uint32_t ReaderPreferences::difference(const ReaderPreferences& other) const {
  uint32_t mask = 0;
  for (size_t i = 0; i < FIELDS.size(); ++i)
    if (values[i] != other.values[i]) mask |= 1U << i;
  if (strcmp(font, other.font) != 0) mask |= FONT_BIT;
  return mask;
}

void ReaderPreferences::write(JsonObject object, uint32_t mask) const {
  for (size_t i = 0; i < FIELDS.size(); ++i) {
    object.remove(FIELDS[i].key);
    if (mask & (1U << i)) object[FIELDS[i].key] = values[i];
  }
  object.remove("sdFontFamilyName");
  if (mask & FONT_BIT) object["sdFontFamilyName"] = font;
}

bool ReaderPreferences::read(JsonObjectConst object, uint32_t& mask) {
  mask = 0;
  for (size_t i = 0; i < FIELDS.size(); ++i) {
    const auto value = object[FIELDS[i].key];
    if (value.isNull()) continue;
    if (!value.is<uint8_t>()) return false;
    const auto numeric = value.as<uint8_t>();
    if (numeric < FIELDS[i].minimum || numeric > FIELDS[i].maximum) return false;
    values[i] = numeric;
    mask |= 1U << i;
  }
  const auto name = object["sdFontFamilyName"];
  if (!name.isNull()) {
    if (!name.is<const char*>() || strlen(name.as<const char*>()) >= sizeof(font)) return false;
    strcpy(font, name.as<const char*>());
    mask |= FONT_BIT;
  }
  return true;
}

void BookReaderSettingsScope::begin(CrossPointSettings& settings, const BookReaderSettings& book) {
  if (enabled) end(settings);
  defaults = ReaderPreferences::capture(settings);
  overrideMask = book.mask & ReaderPreferences::ALL_FIELDS;
  book.preferences.apply(settings, overrideMask);
  lastEffective = ReaderPreferences::capture(settings);
  enabled = true;
}

BookReaderSettings BookReaderSettingsScope::capture(const CrossPointSettings& settings) {
  const auto current = ReaderPreferences::capture(settings);
  const uint32_t changed = current.difference(lastEffective);
  // Unedited overrides stay pinned even when a new global default happens to match them.
  overrideMask = (overrideMask & ~changed) | (current.difference(defaults) & changed);
  lastEffective = current;
  return {current, overrideMask};
}

void BookReaderSettingsScope::reset(CrossPointSettings& settings) {
  if (!enabled) return;
  overrideMask = 0;
  defaults.apply(settings);
  lastEffective = defaults;
}

void BookReaderSettingsScope::end(CrossPointSettings& settings) {
  if (!enabled) return;
  defaults.apply(settings);
  enabled = false;
  overrideMask = 0;
}

void BookReaderSettingsScope::beforeGlobalLoad(CrossPointSettings& settings) {
  if (!enabled) return;
  capture(settings);
  defaults.apply(settings);
}

void BookReaderSettingsScope::afterGlobalLoad(CrossPointSettings& settings) {
  if (!enabled) return;
  defaults = ReaderPreferences::capture(settings);
  lastEffective.apply(settings, overrideMask);
  lastEffective = ReaderPreferences::capture(settings);
}

void BookReaderSettingsScope::writeGlobal(JsonObject object) const {
  if (enabled) defaults.write(object, ReaderPreferences::ALL_FIELDS);
}
