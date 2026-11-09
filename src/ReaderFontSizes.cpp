#include "ReaderFontSizes.h"

#include <iterator>

#include "personalization/ReaderFontStep.h"

std::vector<uint8_t> readerFontPointSizes(const SdCardFontRegistry* registry, const char* sdFamilyName) {
  if (registry && sdFamilyName && sdFamilyName[0] != '\0') {
    if (const auto* family = registry->findFamily(sdFamilyName)) {
      if (family->vector) {
        return {std::begin(VECTOR_READER_POINT_SIZES), std::end(VECTOR_READER_POINT_SIZES)};
      }
      auto sizes = family->availableSizes();
      if (!sizes.empty()) return sizes;
    }
  }
  return {std::begin(BUILTIN_READER_POINT_SIZES), std::end(BUILTIN_READER_POINT_SIZES)};
}

uint8_t snapToNearestPointSize(const uint8_t* sizes, const size_t count, const uint8_t pt) {
  if (!sizes || count == 0) return pt;

  uint8_t best = sizes[0];
  uint8_t bestDelta = best > pt ? best - pt : pt - best;
  for (size_t i = 1; i < count; i++) {
    const uint8_t delta = sizes[i] > pt ? sizes[i] - pt : pt - sizes[i];
    // Strictly-less keeps the smaller size on a tie, since `sizes` is ascending.
    if (delta < bestDelta) {
      best = sizes[i];
      bestDelta = delta;
    }
  }
  return best;
}

bool adjacentReaderFontPointSize(const SdCardFontRegistry* registry, const char* name, uint8_t current, bool larger,
                                 uint8_t& result) {
  if (registry && name && name[0]) {
    for (const auto& family : registry->getFamilies()) {
      if (family.name != name) continue;
      if (family.vector) {
        return readerfonts::adjacentSize(
            std::size(VECTOR_READER_POINT_SIZES), [](size_t i) { return VECTOR_READER_POINT_SIZES[i]; }, current,
            larger, result);
      }
      return readerfonts::adjacentSize(
          family.files.size(), [&family](size_t i) { return family.files[i].pointSize; }, current, larger, result);
    }
  }
  return readerfonts::adjacentSize(
      std::size(BUILTIN_READER_POINT_SIZES), [](size_t i) { return BUILTIN_READER_POINT_SIZES[i]; }, current, larger,
      result);
}
