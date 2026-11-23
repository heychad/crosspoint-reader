#include "FontCacheManager.h"

#include <FontDecompressor.h>
#include <Logging.h>
#include <SdCardFont.h>
#include <TtfEpdFont.h>
#include <Utf8.h>

#include <algorithm>
#include <cstring>

FontCacheManager::FontCacheManager(const std::map<int, EpdFontFamily>& fontMap,
                                   const std::map<int, SdCardFont*>& sdCardFonts,
                                   const std::map<int, TtfEpdFont*>& ttfFonts)
    : fontMap_(fontMap), sdCardFonts_(sdCardFonts), ttfFonts_(ttfFonts) {}

void FontCacheManager::setFontDecompressor(FontDecompressor* d) { fontDecompressor_ = d; }

void FontCacheManager::clearCache() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    font->clearCache();
  }
#if CROSSPOINT_VECTOR_FONTS
  for (auto& [id, font] : ttfFonts_) {
    if (font) font->clearCache();
  }
#endif
}

void FontCacheManager::releaseSdFontCaches() {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  for (auto& [id, font] : sdCardFonts_) {
    font->releaseResidentCaches();
  }
#if CROSSPOINT_VECTOR_FONTS
  for (auto& [id, font] : ttfFonts_) {
    if (font) font->releaseResidentCaches();
  }
#endif
}

void FontCacheManager::releaseInactiveSdFontCaches(const int activeFontId) {
  if (fontDecompressor_) fontDecompressor_->clearCache();
  const auto active = sdCardFonts_.find(activeFontId);
  const auto* activeFont = active != sdCardFonts_.end() ? active->second : nullptr;
  for (const auto& [id, font] : sdCardFonts_) {
    if (font && font != activeFont) font->releaseResidentCaches();
  }
}

void FontCacheManager::prewarmCache(int fontId, const char* utf8Text, uint8_t styleMask, bool accumulate) {
  // TTF (vector) font prewarm path. This is the single dispatch every draw path
  // funnels through (reader endScanAndPrewarm, the settings preview, UI text),
  // so building here covers them all. accumulate=false means "this is the whole
  // glyph set for this render" → replace; accumulate=true → add incrementally.
  // styleMask is ignored: a TTF face has no synthesized bold/italic here.
#if CROSSPOINT_VECTOR_FONTS
  auto tit = ttfFonts_.find(fontId);
  if (tit != ttfFonts_.end() && tit->second) {
    if (accumulate) {
      tit->second->addCoverage(utf8Text);
    } else {
      tit->second->build(utf8Text);
    }
    return;
  }
#endif

  // SD card font prewarm path: prewarm all requested styles in one call
  auto it = sdCardFonts_.find(fontId);
  if (it != sdCardFonts_.end()) {
    int missed = it->second->prewarm(utf8Text, styleMask, /*metadataOnly=*/false, /*loadKernLig=*/true, accumulate);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache(SD): %d glyph(s) not found (styleMask=0x%02X)", missed, styleMask);
    }
    return;
  }

  // Standard compressed font prewarm path: loop over all requested styles
  if (!fontDecompressor_ || fontMap_.count(fontId) == 0) return;

  for (uint8_t i = 0; i < 4; i++) {
    if (!(styleMask & (1 << i))) continue;
    auto style = static_cast<EpdFontFamily::Style>(i);
    const EpdFontData* data = fontMap_.at(fontId).getData(style);
    if (!data || !data->groups) continue;
    int missed = fontDecompressor_->prewarmCache(data, utf8Text);
    if (missed > 0) {
      LOG_DBG("FCM", "prewarmCache: %d glyph(s) not cached for style %d", missed, i);
    }
  }
}

void FontCacheManager::logStats(const char* label) {
  if (fontDecompressor_) fontDecompressor_->logStats(label);
  for (auto& [id, font] : sdCardFonts_) {
    font->logStats(label);
  }
}

void FontCacheManager::resetStats() {
  if (fontDecompressor_) fontDecompressor_->resetStats();
  for (auto& [id, font] : sdCardFonts_) {
    font->resetStats();
  }
}

bool FontCacheManager::isScanning() const { return scanMode_ == ScanMode::Scanning; }

uint8_t FontCacheManager::resolveScanStyle(int fontId, EpdFontFamily::Style style) const {
  const uint8_t baseStyle = static_cast<uint8_t>(style) & 0x03;

  const auto sdFont = sdCardFonts_.find(fontId);
  if (sdFont != sdCardFonts_.end()) return sdFont->second->resolveStyle(baseStyle);

  const auto font = fontMap_.find(fontId);
  if (font == fontMap_.end()) return baseStyle;

  const EpdFontData* resolvedData = font->second.getData(static_cast<EpdFontFamily::Style>(baseStyle));
  for (uint8_t candidate = 0; candidate < 4; candidate++) {
    if (font->second.getData(static_cast<EpdFontFamily::Style>(candidate)) == resolvedData) return candidate;
  }
  return baseStyle;
}

void FontCacheManager::recordText(const char* text, int fontId, EpdFontFamily::Style style) {
  if (!text || *text == '\0') return;

  uint8_t fontSlot = scanFontCount_;
  for (uint8_t i = 0; i < scanFontCount_; i++) {
    if (scanFontIds_[i] == fontId) {
      fontSlot = i;
      break;
    }
  }
  if (fontSlot == scanFontCount_) {
    if (scanFontCount_ >= MAX_SCAN_FONTS) return;
    scanFontIds_[scanFontCount_++] = fontId;
  }

  const uint8_t resolvedStyle = resolveScanStyle(fontId, style);
  const uint8_t group = fontSlot * 4 + resolvedStyle;
  const unsigned char* cursor = reinterpret_cast<const unsigned char*>(text);
  while (*cursor) {
    uint32_t codepoint = utf8NextCodepoint(&cursor);
    if (codepoint == 0) break;
    // Small caps draw lowercase letters with their uppercase glyphs.
    if (style & EpdFontFamily::SMALL_CAPS) codepoint = utf8SmallCapsUpper(codepoint);
    if (scanGroupUses_[group] != UINT16_MAX) ++scanGroupUses_[group];

    const uint32_t packed = (static_cast<uint32_t>(fontSlot) << SCAN_FONT_SHIFT) |
                            (static_cast<uint32_t>(resolvedStyle) << SCAN_STYLE_SHIFT) | codepoint;
    bool found = false;
    for (uint16_t i = 0; i < scanCodepointCount_; i++) {
      if (scanCodepoints_[i] == packed) {
        found = true;
        break;
      }
    }
    if (found) continue;

    if (scanCodepointCount_ >= MAX_SCAN_CODEPOINTS) {
      if (!scanOverflowWarned_) {
        LOG_DBG("FCM", "Scan codepoint cap (%u) reached; excess glyphs will load on demand",
                static_cast<unsigned>(MAX_SCAN_CODEPOINTS));
        scanOverflowWarned_ = true;
      }
      continue;
    }

    scanCodepoints_[scanCodepointCount_++] = packed;
    scanGroupCounts_[group]++;
  }
}

// --- PrewarmScope implementation ---

FontCacheManager::PrewarmScope::PrewarmScope(FontCacheManager& manager) : manager_(&manager) {
  manager_->scanMode_ = ScanMode::Scanning;
  manager_->clearCache();
  manager_->resetStats();
  manager_->scanCodepointCount_ = 0;
  manager_->scanFontCount_ = 0;
  manager_->scanOverflowWarned_ = false;
  memset(manager_->scanGroupCounts_, 0, sizeof(manager_->scanGroupCounts_));
  memset(manager_->scanGroupUses_, 0, sizeof(manager_->scanGroupUses_));
}

void FontCacheManager::PrewarmScope::endScanAndPrewarm() {
  manager_->scanMode_ = ScanMode::None;
  if (manager_->scanCodepointCount_ == 0) return;

  // Keep SD/vector prewarm in place while ordering the compressed styles
  // that compete for the decompressor's page slots.
  uint8_t groupOrder[SCAN_GROUP_COUNT];
  uint8_t compressed[SCAN_GROUP_COUNT];
  static_assert(SCAN_GROUP_COUNT <= 16);
  uint16_t compressedMask = 0;
  uint8_t compressedCount = 0;
  const bool canExceedSlots =
      manager_->fontDecompressor_ && manager_->scanFontCount_ * 4 > FontDecompressor::MAX_PAGE_SLOTS;
  for (uint8_t group = 0; group < SCAN_GROUP_COUNT; ++group) {
    groupOrder[group] = group;
    if (!canExceedSlots || !manager_->scanGroupCounts_[group]) continue;
    const int fontId = manager_->scanFontIds_[group / 4];
#if CROSSPOINT_VECTOR_FONTS
    const auto ttf = manager_->ttfFonts_.find(fontId);
    if (ttf != manager_->ttfFonts_.end() && ttf->second) continue;
#endif
    if (manager_->sdCardFonts_.count(fontId)) continue;
    const auto font = manager_->fontMap_.find(fontId);
    if (font == manager_->fontMap_.end()) continue;
    const auto* data = font->second.getData(static_cast<EpdFontFamily::Style>(group & 3));
    if (!data || !data->groups) continue;
    compressed[compressedCount++] = group;
    compressedMask |= 1U << group;
  }
  auto* const begin = manager_->scanCodepoints_;
  auto* const end = begin + manager_->scanCodepointCount_;
  if (compressedCount > FontDecompressor::MAX_PAGE_SLOTS) {
    // Groups are consumed backwards. Equal usage favors the first scanned
    // font, then regular style.
    std::sort(compressed, compressed + compressedCount, [&](uint8_t a, uint8_t b) {
      if (manager_->scanGroupUses_[a] != manager_->scanGroupUses_[b])
        return manager_->scanGroupUses_[a] < manager_->scanGroupUses_[b];
      return a > b;
    });
    uint8_t next = 0;
    uint8_t rank[SCAN_GROUP_COUNT];
    for (uint8_t group = 0; group < SCAN_GROUP_COUNT; ++group) {
      if (compressedMask & (1U << group)) groupOrder[group] = compressed[next++];
      rank[groupOrder[group]] = group;
    }
    std::sort(begin, end, [&](uint32_t a, uint32_t b) {
      const uint32_t groupA = a >> SCAN_STYLE_SHIFT, groupB = b >> SCAN_STYLE_SHIFT;
      return groupA == groupB ? a < b : rank[groupA] < rank[groupB];
    });
  } else {
    std::sort(begin, end);
  }

  uint16_t groupStarts[SCAN_GROUP_COUNT];
  uint16_t offset = 0;
  for (const uint8_t group : groupOrder) {
    groupStarts[group] = offset;
    offset += manager_->scanGroupCounts_[group];
  }

  // Each packed entry provides four bytes, enough for one UTF-8 codepoint.
  // Encoding from the end means a terminator can overwrite only a group
  // that has already been prewarmed; unread lower groups remain intact.
  for (int position = SCAN_GROUP_COUNT - 1; position >= 0; --position) {
    const uint8_t group = groupOrder[position];
    const uint16_t groupCount = manager_->scanGroupCounts_[group];
    if (groupCount == 0) continue;

    const uint16_t groupStart = groupStarts[group];
    char* const utf8Text = reinterpret_cast<char*>(manager_->scanCodepoints_ + groupStart);
    char* output = utf8Text;
    for (uint16_t i = 0; i < groupCount; i++) {
      const uint32_t codepoint = manager_->scanCodepoints_[groupStart + i] & SCAN_CODEPOINT_MASK;
      output = utf8AppendCodepoint(output, codepoint);
    }
    *output = '\0';

    const uint8_t fontSlot = group / 4;
    const uint8_t style = group & 0x03;
    // This group is the complete glyph set for one font/style in this render.
    manager_->prewarmCache(manager_->scanFontIds_[fontSlot], utf8Text, 1 << style, /*accumulate=*/false);
  }

  manager_->scanCodepointCount_ = 0;
  manager_->scanFontCount_ = 0;
  memset(manager_->scanGroupCounts_, 0, sizeof(manager_->scanGroupCounts_));
}

FontCacheManager::PrewarmScope::~PrewarmScope() {
  if (active_) {
    endScanAndPrewarm();  // no-op if already called
    manager_->clearCache();
  }
}

FontCacheManager::PrewarmScope::PrewarmScope(PrewarmScope&& other) noexcept
    : manager_(other.manager_), active_(other.active_) {
  other.active_ = false;
}

FontCacheManager::PrewarmScope FontCacheManager::createPrewarmScope() { return PrewarmScope(*this); }
