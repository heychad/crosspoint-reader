#include "ReferencePageMap.h"

#include <Memory.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string_view>

void ReferencePageMap::clear() {
  ranges.reset();
  spineCount = 0;
  totalUnits = unitsPerPage = pages = 0;
  characters = valid = false;
}

std::optional<ReferencePageMap::Target> ReferencePageMap::target(const uint32_t page) const {
  if (!valid) return std::nullopt;
  const uint32_t clamped = std::clamp<uint32_t>(page, 1, pages);
  const uint32_t unit =
      static_cast<uint32_t>(std::min<uint64_t>(static_cast<uint64_t>(clamped - 1) * unitsPerPage, totalUnits - 1));
  return targetUnit(unit);
}

std::optional<ReferencePageMap::Position> ReferencePageMap::targetPercent(const uint8_t percent) const {
  if (!valid || percent > 100) return std::nullopt;
  const double units = static_cast<double>(totalUnits) * percent / 100;
  const auto target = targetUnit(static_cast<uint32_t>(std::min<double>(units, totalUnits - 1)));
  if (!target) return std::nullopt;
  const auto& range = ranges[target->spineIndex];
  const uint32_t start = characters ? range.characterStart : range.wordStart;
  const auto fraction = static_cast<float>(std::clamp((units - start) / target->unitCount, 0.0, 1.0));
  return Position{target->spineIndex, fraction};
}

std::optional<ReferencePageMap::Target> ReferencePageMap::targetUnit(const uint32_t unit) const {
  std::optional<Target> last;
  for (size_t i = 0; i < spineCount; ++i) {
    const auto& range = ranges[i];
    const uint32_t start = characters ? range.characterStart : range.wordStart;
    const uint32_t count = characters ? range.characterCount : range.wordCount;
    if (count == 0) continue;
    if (unit < start) return Target{static_cast<int>(i), 0, count, characters};
    if (unit - start < count) return Target{static_cast<int>(i), unit - start, count, characters};
    last = Target{static_cast<int>(i), count, count, characters};
  }
  return last;
}

bool ReferencePageMap::resolve(const int spineIndex, const float spineProgress, uint32_t& page,
                               uint32_t& pageTotal) const {
  page = pageTotal = 0;
  if (!valid || spineIndex < 0 || static_cast<size_t>(spineIndex) >= spineCount || !std::isfinite(spineProgress))
    return false;
  const auto& range = ranges[spineIndex];
  const uint32_t start = characters ? range.characterStart : range.wordStart;
  const uint32_t count = characters ? range.characterCount : range.wordCount;
  if (count == 0) return false;
  const double progress = std::clamp(static_cast<double>(spineProgress), 0.0, 1.0);
  const uint64_t unit = start + static_cast<uint64_t>(progress * count);
  page = static_cast<uint32_t>(std::min<uint64_t>(unit, totalUnits - 1) / unitsPerPage + 1);
  pageTotal = pages;
  return true;
}

ReferencePageParser::ReferencePageParser(ReferencePageMap& destination, const size_t spineCount)
    : destination(destination),
      parser({this, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart, onArrayEnd}) {
  destination.clear();
  if (spineCount == 0 || spineCount > ReferencePageMap::MAX_SPINE_ENTRIES) {
    failed = true;
    return;
  }
  destination.ranges = makeUniqueNoThrow<ReferencePageMap::Range[]>(spineCount);
  destination.spineCount = spineCount;
  failed = !destination.ranges;
}

bool ReferencePageParser::ready() const { return !failed && !parser.hasError(); }

size_t ReferencePageParser::write(const uint8_t value) { return write(&value, 1); }

size_t ReferencePageParser::write(const uint8_t* data, const size_t length) {
  if (!ready() || length > ReferencePageMap::MAX_MANIFEST_BYTES - bytesRead) {
    failed = true;
    return 0;
  }
  bytesRead += length;
  parser.feed(reinterpret_cast<const char*>(data), length);
  return ready() ? length : 0;
}

void ReferencePageParser::onKey(void* context, const char* value, const size_t length) {
  auto& self = *static_cast<ReferencePageParser*>(context);
  self.key = Key::None;
  const std::string_view name(value, length);
  static constexpr std::string_view names[] = {"",
                                               "format",
                                               "version",
                                               "totalLocations",
                                               "totalWords",
                                               "totalCharacters",
                                               "wordsPerReferencePage",
                                               "charactersPerReferencePage",
                                               "totalReferencePages",
                                               "spine",
                                               "index",
                                               "wordStart",
                                               "wordCount",
                                               "characterStart",
                                               "characterCount"};
  const size_t first = self.depth == 1 ? 1 : self.entryOpen && self.depth == 3 ? 10 : 0;
  const size_t last = first == 1 ? 10 : first == 10 ? std::size(names) : 0;
  for (size_t i = first; i < last; ++i) {
    if (name != names[i]) continue;
    self.key = static_cast<Key>(i);
    auto& seen = self.depth == 1 ? self.rootKeys : self.entryKeys;
    const uint32_t bit = 1U << i;
    if (seen & bit) self.failed = true;
    seen |= bit;
    break;
  }
}

void ReferencePageParser::scalar() {
  if (depth == 0 || (spineOpen && depth == 2) || key != Key::None) failed = true;
  key = Key::None;
}

void ReferencePageParser::onString(void* context, const char* value, const size_t length) {
  auto& self = *static_cast<ReferencePageParser*>(context);
  if (self.key == Key::Format) {
    const std::string_view format(value, length);
    self.formatValid = format == "x-locations" || format == "crossink-locations";
    self.key = Key::None;
  }
  self.scalar();
}

void ReferencePageParser::onNumber(void* context, const char* value, const size_t length) {
  auto& self = *static_cast<ReferencePageParser*>(context);
  if (self.key == Key::None) {
    self.scalar();
    return;
  }
  uint32_t number = 0;
  for (size_t i = 0; i < length; ++i) {
    if (value[i] < '0' || value[i] > '9' || number > (UINT32_MAX - (value[i] - '0')) / 10) {
      self.failed = true;
      return;
    }
    number = number * 10 + value[i] - '0';
  }
  switch (self.key) {
    case Key::Version:
      self.version = number;
      break;
    case Key::TotalLocations:
      self.totalLocations = number;
      break;
    case Key::TotalWords:
      self.totalWords = number;
      break;
    case Key::TotalCharacters:
      self.totalCharacters = number;
      break;
    case Key::WordsPerPage:
      self.wordsPerPage = number;
      break;
    case Key::CharactersPerPage:
      self.charactersPerPage = number;
      break;
    case Key::TotalPages:
      self.destination.pages = number;
      break;
    case Key::Index:
      self.index = number;
      break;
    case Key::WordStart:
      self.range.wordStart = number;
      break;
    case Key::WordCount:
      self.range.wordCount = number;
      break;
    case Key::CharacterStart:
      self.range.characterStart = number;
      break;
    case Key::CharacterCount:
      self.range.characterCount = number;
      break;
    case Key::None:
    case Key::Format:
    case Key::Spine:
      self.failed = true;
      break;
  }
  self.key = Key::None;
}

void ReferencePageParser::onBool(void* context, bool) { static_cast<ReferencePageParser*>(context)->scalar(); }
void ReferencePageParser::onNull(void* context) { static_cast<ReferencePageParser*>(context)->scalar(); }
void ReferencePageParser::onObjectStart(void* context) {
  static_cast<ReferencePageParser*>(context)->start(Container::Object);
}
void ReferencePageParser::onArrayStart(void* context) {
  static_cast<ReferencePageParser*>(context)->start(Container::Array);
}
void ReferencePageParser::onObjectEnd(void* context) {
  static_cast<ReferencePageParser*>(context)->end(Container::Object);
}
void ReferencePageParser::onArrayEnd(void* context) {
  static_cast<ReferencePageParser*>(context)->end(Container::Array);
}

void ReferencePageParser::start(const Container container) {
  if (rootClosed || depth >= containers.size() || (depth == 0 && container != Container::Object)) {
    failed = true;
    return;
  }
  if (key == Key::Spine && container == Container::Array) {
    spineOpen = true;
  } else if (key != Key::None) {
    failed = true;
  }
  if (spineOpen && depth == 2) {
    if (container != Container::Object || ordinal >= destination.spineCount) {
      failed = true;
    } else {
      entryOpen = true;
      range = {};
      entryKeys = 0;
      index = static_cast<uint32_t>(ordinal);
    }
  }
  key = Key::None;
  containers[depth++] = container;
}

void ReferencePageParser::commitRange() {
  if (index >= destination.spineCount || seenEntries.test(index)) {
    failed = true;
    return;
  }
  destination.ranges[index] = range;
  seenEntries.set(index);
  ++ordinal;
}

void ReferencePageParser::end(const Container container) {
  if (depth == 0 || containers[depth - 1] != container || key != Key::None) {
    failed = true;
    return;
  }
  if (entryOpen && depth == 3) {
    commitRange();
    entryOpen = false;
  }
  if (spineOpen && depth == 2) spineOpen = false;
  if (--depth == 0) rootClosed = true;
  key = Key::None;
}

bool ReferencePageParser::finish() {
  auto& result = destination;
  result.characters = totalCharacters > 0 && charactersPerPage > 0;
  result.totalUnits = result.characters ? totalCharacters : totalWords;
  result.unitsPerPage = result.characters ? charactersPerPage : wordsPerPage;
  if (!ready() || !rootClosed || depth != 0 || !formatValid || version != 1 || totalLocations == 0 || ordinal == 0 ||
      result.totalUnits == 0 || result.unitsPerPage == 0) {
    result.clear();
    return false;
  }
  const uint32_t computedPages =
      static_cast<uint32_t>((static_cast<uint64_t>(result.totalUnits) + result.unitsPerPage - 1) / result.unitsPerPage);
  if (result.pages != 0 && result.pages != computedPages) {
    result.clear();
    return false;
  }
  uint32_t previousEnd = 0;
  for (size_t i = 0; i < result.spineCount; ++i) {
    const auto& item = result.ranges[i];
    const uint32_t start = result.characters ? item.characterStart : item.wordStart;
    const uint32_t count = result.characters ? item.characterCount : item.wordCount;
    if (count == 0) continue;
    if (start < previousEnd || start >= result.totalUnits || count > result.totalUnits - start) {
      result.clear();
      return false;
    }
    previousEnd = start + count;
  }
  if (previousEnd == 0) {
    result.clear();
    return false;
  }
  result.pages = computedPages;
  result.valid = true;
  return true;
}
