#include <gtest/gtest.h>

#include <cstdlib>
#include <map>
#include <new>

#include "FontCacheManager.h"
#include "FontDecompressor.h"
#include "SdCardFont.h"

namespace {

bool countHeapAllocations = false;
size_t heapAllocationCount = 0;

// TTF fonts are PSRAM-gated and stubbed out on the host (see stubs/TtfEpdFont.h);
// every manager gets an empty map for that slot.
const std::map<int, TtfEpdFont*> kNoTtfFonts;

const SdCardFont::PrewarmCall* findCall(const SdCardFont& font, const uint8_t styleMask) {
  for (int i = 0; i < font.prewarmCallCount; i++) {
    if (font.prewarmCalls[i].styleMask == styleMask) return &font.prewarmCalls[i];
  }
  return nullptr;
}

}  // namespace

void* operator new(const size_t size) {
  if (countHeapAllocations) heapAllocationCount++;
  if (void* allocation = std::malloc(size)) return allocation;
  throw std::bad_alloc();
}

void operator delete(void* allocation) noexcept { std::free(allocation); }

void operator delete(void* allocation, size_t) noexcept { std::free(allocation); }

TEST(FontCacheManagerTest, PrewarmScopeBatchesEachFontAndResolvedStyleSeparately) {
  SdCardFont readerFont;
  SdCardFont fallbackFont;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{-17, &readerFont}, {23, &fallbackFont}};
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", -17, EpdFontFamily::REGULAR);
  manager.recordText("B", -17, EpdFontFamily::BOLD);
  manager.recordText("C", 23, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2, readerFont.prewarmCallCount);
  const auto* regularCall = findCall(readerFont, 0x01);
  ASSERT_NE(nullptr, regularCall);
  EXPECT_STREQ("A", regularCall->text);
  EXPECT_FALSE(regularCall->accumulate);
  const auto* boldCall = findCall(readerFont, 0x02);
  ASSERT_NE(nullptr, boldCall);
  EXPECT_STREQ("B", boldCall->text);
  EXPECT_FALSE(boldCall->accumulate);

  ASSERT_EQ(1, fallbackFont.prewarmCallCount);
  EXPECT_STREQ("C", fallbackFont.prewarmCalls[0].text);
  EXPECT_EQ(0x01, fallbackFont.prewarmCalls[0].styleMask);
}

TEST(FontCacheManagerTest, BackgroundReclaimKeepsActiveFontAndAliasesResident) {
  SdCardFont active;
  SdCardFont inactive;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{-17, &active}, {23, &active}, {27, &inactive}};
  FontDecompressor decompressor;
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);
  manager.setFontDecompressor(&decompressor);

  heapAllocationCount = 0;
  countHeapAllocations = true;
  manager.releaseInactiveSdFontCaches(-17);
  countHeapAllocations = false;
  EXPECT_EQ(heapAllocationCount, 0u);
  EXPECT_EQ(decompressor.clearCount, 1);
  EXPECT_EQ(active.residentReleaseCount, 0);
  EXPECT_EQ(inactive.residentReleaseCount, 1);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesStylesThatResolveToTheSameSdFontData) {
  SdCardFont font;
  font.resolvedStyles[EpdFontFamily::BOLD] = EpdFontFamily::REGULAR;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 7, EpdFontFamily::REGULAR);
  manager.recordText("B", 7, EpdFontFamily::BOLD);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_STREQ("AB", font.prewarmCalls[0].text);
  EXPECT_EQ(0x01, font.prewarmCalls[0].styleMask);
}

TEST(FontCacheManagerTest, PrewarmScopeMergesBuiltInStylesThatShareFontData) {
  EpdFontData regularData{reinterpret_cast<const void*>(1)};
  EpdFontData boldData{reinterpret_cast<const void*>(1)};
  const EpdFontFamily family(&regularData, &boldData, &regularData, nullptr);
  const std::map<int, EpdFontFamily> builtinFonts{{5, family}};
  const std::map<int, SdCardFont*> noSdFonts;
  FontDecompressor decompressor;
  FontCacheManager manager(builtinFonts, noSdFonts, kNoTtfFonts);
  manager.setFontDecompressor(&decompressor);

  auto scope = manager.createPrewarmScope();
  manager.recordText("A", 5, EpdFontFamily::REGULAR);
  manager.recordText("B", 5, EpdFontFamily::BOLD);
  manager.recordText("C", 5, EpdFontFamily::ITALIC);
  scope.endScanAndPrewarm();

  ASSERT_EQ(2, decompressor.prewarmCallCount);
  const auto& first = decompressor.prewarmCalls[0];
  const auto& second = decompressor.prewarmCalls[1];
  const auto* regularCall = first.fontData == &regularData ? &first : &second;
  const auto* boldCall = first.fontData == &boldData ? &first : &second;
  EXPECT_STREQ("AC", regularCall->text);
  EXPECT_STREQ("B", boldCall->text);
}

TEST(FontCacheManagerTest, PrewarmScopePreservesUniqueMultibyteCodepoints) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  auto scope = manager.createPrewarmScope();
  manager.recordText("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80\xC3\xA9", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();

  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_STREQ("\xC3\xA9\xE4\xB8\xAD\xF0\x9F\x98\x80", font.prewarmCalls[0].text);
}

TEST(FontCacheManagerTest, PrewarmScopePrioritizesFrequentCompressedStylesUnderSlotPressure) {
  EpdFontData styles[12];
  for (auto& font : styles) font.groups = reinterpret_cast<const void*>(1);
  const std::map<int, EpdFontFamily> builtinFonts{
      {5, EpdFontFamily(&styles[0], &styles[1], &styles[2], &styles[3])},
      {20, EpdFontFamily(&styles[4], &styles[5], &styles[6], &styles[7])},
      {30, EpdFontFamily(&styles[8], &styles[9], &styles[10], &styles[11])},
  };
  SdCardFont sdFont;
  const std::map<int, SdCardFont*> sdFonts{{11, &sdFont}};
  FontDecompressor decompressor;
  int order = 0;
  decompressor.order = &order;
  sdFont.order = &order;
  FontCacheManager manager(builtinFonts, sdFonts, kNoTtfFonts);
  manager.setFontDecompressor(&decompressor);

  heapAllocationCount = 0;
  countHeapAllocations = true;
  auto scope = manager.createPrewarmScope();
  manager.recordText("AAAAAAAA", 5, EpdFontFamily::REGULAR);
  manager.recordText("sd", 11, EpdFontFamily::REGULAR);
  for (int fontId : {5, 20, 30}) {
    for (auto style :
         {EpdFontFamily::REGULAR, EpdFontFamily::BOLD, EpdFontFamily::ITALIC, EpdFontFamily::BOLD_ITALIC}) {
      manager.recordText("a", fontId, style);
    }
  }
  manager.recordText("bb", 5, EpdFontFamily::BOLD);
  manager.recordText("\xF0\x9F\x98\x80\xF0\x9F\x98\x80\xF0\x9F\x98\x80", 30, EpdFontFamily::ITALIC);
  scope.endScanAndPrewarm();
  countHeapAllocations = false;

  EXPECT_EQ(heapAllocationCount, 0u);
  ASSERT_EQ(decompressor.prewarmCallCount, 12);
  const int admittedStyles[] = {0, 10, 1, 2, 3, 4, 5, 6};
  for (int i = 0; i < FontDecompressor::MAX_PAGE_SLOTS; ++i) {
    EXPECT_TRUE(decompressor.prewarmCalls[i].admitted);
    EXPECT_EQ(decompressor.prewarmCalls[i].fontData, &styles[admittedStyles[i]]);
  }
  for (int i = FontDecompressor::MAX_PAGE_SLOTS; i < decompressor.prewarmCallCount; ++i) {
    EXPECT_FALSE(decompressor.prewarmCalls[i].admitted);
  }
  EXPECT_STREQ("Aa", decompressor.prewarmCalls[0].text);
  EXPECT_STREQ("a\xF0\x9F\x98\x80", decompressor.prewarmCalls[1].text);
  ASSERT_EQ(sdFont.prewarmCallCount, 1);
  EXPECT_STREQ("ds", sdFont.prewarmCalls[0].text);
  EXPECT_EQ(sdFont.prewarmPosition, 8);
}

TEST(FontCacheManagerTest, PrewarmScanDoesNotAllocateHeapMemory) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);

  heapAllocationCount = 0;
  countHeapAllocations = true;
  auto scope = manager.createPrewarmScope();
  manager.recordText("Repeated text: \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80", 7, EpdFontFamily::REGULAR);
  scope.endScanAndPrewarm();
  countHeapAllocations = false;

  EXPECT_EQ(0U, heapAllocationCount);
}

TEST(FontCacheManagerTest, IncrementalPrewarmRequestsAccumulation) {
  SdCardFont font;
  const std::map<int, EpdFontFamily> noBuiltinFonts;
  const std::map<int, SdCardFont*> sdFonts{{7, &font}};
  FontCacheManager manager(noBuiltinFonts, sdFonts, kNoTtfFonts);
  manager.prewarmCache(7, "title", 1);
  ASSERT_EQ(1, font.prewarmCallCount);
  EXPECT_TRUE(font.prewarmCalls[0].accumulate);
}
