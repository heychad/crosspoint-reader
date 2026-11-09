#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include "activities/reader/BookReaderPreferences.h"
#include "activities/reader/ReadingStatsStore.h"
#include "personalization/BookReaderSettings.h"

namespace {
constexpr const char* CACHE = "/.crosspoint/epub_123";
constexpr const char* PATH = "/.crosspoint/reading_stats/epub_123/reader_prefs.json";
std::string crossInkV10() {
  // Font size 18 and orientation 1 are the two explicitly owned fields.
  std::string data;
  for (uint8_t value : {10, 1, 0, 0, 0, 0, 18, 110, 0, 1, 7, 7, 0, 0, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0})
    data.push_back(static_cast<char>(value));
  data.append(128, '\0');  // Reader and dictionary font names.
  data.push_back(0);       // Dictionary point size.
  for (uint8_t value : {0x12, 0, 0, 0}) data.push_back(static_cast<char>(value));
  return data;
}
void put(const std::string& path, const std::string& contents) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", path, file));
  ASSERT_EQ(file.write(contents.data(), contents.size()), contents.size());
}
BookReaderSettings sizeOverride() {
  CrossPointSettings settings;
  settings.fontPointSize = 18;
  auto preferences = ReaderPreferences::capture(settings);
  CrossPointSettings defaults;
  return {preferences, preferences.difference(ReaderPreferences::capture(defaults))};
}
class BookSettings : public testing::Test {
  void SetUp() override { Storage.reset(); }
};
}  // namespace

TEST(ReaderPreferenceScope, EditsStayWithBookAndGlobalSaveUsesDefaults) {
  CrossPointSettings settings;
  BookReaderSettingsScope scope;
  scope.begin(settings, {});
  settings.fontPointSize = 18;
  auto saved = scope.capture(settings);
  EXPECT_NE(saved.mask, 0U);
  JsonDocument global;
  scope.writeGlobal(global.to<JsonObject>());
  EXPECT_EQ(global["fontSize"].as<int>(), 14);
  scope.end(settings);
  EXPECT_EQ(settings.fontPointSize, 14);
  scope.begin(settings, saved);
  EXPECT_EQ(settings.fontPointSize, 18);
}

TEST(ReaderPreferenceScope, UneditedOverridesStayPinnedAcrossMatchingGlobalChange) {
  CrossPointSettings settings;
  BookReaderSettingsScope scope;
  scope.begin(settings, sizeOverride());
  scope.beforeGlobalLoad(settings);
  EXPECT_EQ(settings.fontPointSize, 14);
  settings.fontPointSize = 18;
  settings.lineSpacing = 5;
  scope.afterGlobalLoad(settings);
  auto saved = scope.capture(settings);
  EXPECT_NE(saved.mask, 0U);
  EXPECT_EQ(settings.lineSpacing, 5);
  scope.beforeGlobalLoad(settings);
  settings.fontPointSize = 12;
  scope.afterGlobalLoad(settings);
  EXPECT_EQ(settings.fontPointSize, 18);
  EXPECT_EQ(scope.capture(settings).mask, saved.mask);
  scope.end(settings);
  EXPECT_EQ(settings.fontPointSize, 12);
}

TEST(ReaderPreferenceScope, ResetAndReturningValueToDefaultRemoveOverrides) {
  CrossPointSettings settings;
  BookReaderSettingsScope scope;
  scope.begin(settings, sizeOverride());
  settings.fontPointSize = 14;
  EXPECT_EQ(scope.capture(settings).mask, 0U);
  settings.fontPointSize = 16;
  EXPECT_NE(scope.capture(settings).mask, 0U);
  scope.reset(settings);
  EXPECT_EQ(settings.fontPointSize, 14);
  EXPECT_EQ(scope.capture(settings).mask, 0U);
}

TEST(ReaderPreferences, RoundTripAllFieldsAndFont) {
  CrossPointSettings settings;
  settings.fontPointSize = 22;
  strcpy(settings.sdFontFamilyName, "Literata");
  auto snapshot = ReaderPreferences::capture(settings);
  JsonDocument doc;
  snapshot.write(doc.to<JsonObject>(), ReaderPreferences::ALL_FIELDS);
  ReaderPreferences result;
  uint32_t mask;
  ASSERT_TRUE(result.read(doc.as<JsonObjectConst>(), mask));
  EXPECT_EQ(mask, ReaderPreferences::ALL_FIELDS);
  EXPECT_EQ(result.difference(snapshot), 0U);
}

TEST(ReaderPreferences, RejectsMalformedValuesAndOversizedFont) {
  for (const char* json :
       {"{\"fontSize\":1}", "{\"lineHeight\":8}", "{\"wordSpacing\":201}", "{\"focusReadingEnabled\":\"yes\"}",
        "{\"orientation\":-1}", "{\"sdFontFamilyName\":\"1234567890123456789012345678901234\"}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    ReaderPreferences result;
    uint32_t mask;
    EXPECT_FALSE(result.read(doc.as<JsonObjectConst>(), mask)) << json;
  }
}

TEST_F(BookSettings, SavesLoadsAndResetsSparsePreferences) {
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  ASSERT_TRUE(store.save({}));
  EXPECT_FALSE(Storage.exists(PATH));
  ASSERT_TRUE(store.save(sizeOverride()));
  BookReaderPreferences second;
  ASSERT_TRUE(second.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.mask, sizeOverride().mask);
  EXPECT_EQ(saved.preferences.difference(sizeOverride().preferences) & saved.mask, 0U);
  ASSERT_TRUE(second.save({}));
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.mask, 0U);
}

TEST_F(BookSettings, LegacySettingsApplyWithoutWritingAndResetIsRemembered) {
  const std::string legacy = std::string(CACHE) + "/reader_settings.bin";
  const auto original = crossInkV10();
  put(legacy, original);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, 1U << 4);
  EXPECT_EQ(settings.preferences.values[4], 1);
  ASSERT_TRUE(store.save(settings));
  EXPECT_FALSE(Storage.exists(PATH));
  ASSERT_TRUE(store.save({}));  // Reset to defaults, including an empty marker.
  ASSERT_TRUE(Storage.exists(PATH));
  BookReaderPreferences reopened;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, 0U);
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 256, after));
  EXPECT_EQ(after, original);
  EXPECT_FALSE(BookReaderPreferences::prepareMove(CACHE, "/book.epub"));
}

TEST_F(BookSettings, ExistingNativePreferencesWinOverLegacy) {
  put(std::string(CACHE) + "/reader_settings.bin", crossInkV10());
  put(PATH, R"({"version":1,"overrides":{"fontSize":16}})");
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, 1U);
  EXPECT_EQ(settings.preferences.values[0], 16);
  EXPECT_FALSE(BookReaderPreferences::prepareMove(CACHE, "/book.epub"));
}

TEST_F(BookSettings, InvalidLegacyAndRecoveryArtifactsBlockWrites) {
  const std::string legacy = std::string(CACHE) + "/reader_settings.bin";
  for (bool recoveryOnly : {false, true}) {
    Storage.reset();
    put(legacy + (recoveryOnly ? ".tmp" : ""), "broken");
    BookReaderPreferences store;
    BookReaderSettings settings;
    EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
    EXPECT_FALSE(store.save(sizeOverride()));
    EXPECT_FALSE(Storage.exists(PATH));
  }
}

TEST_F(BookSettings, LegacySafeModeIsNotPartiallyApplied) {
  auto record = crossInkV10();
  record[1] |= 0x10;
  put(std::string(CACHE) + "/reader_settings.bin", record);
  BookReaderPreferences store;
  BookReaderSettings settings;
  EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, 0U);
  EXPECT_FALSE(store.save(sizeOverride()));
}

TEST_F(BookSettings, LegacyFontSizeDoesNotChangeOrTriggerSaveDuringFontFallback) {
  auto record = crossInkV10();
  record[6] = 10;
  put(std::string(CACHE) + "/reader_settings.bin", record);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask & 1U, 0U);
  CrossPointSettings effective;
  BookReaderSettingsScope scope;
  scope.begin(effective, settings);
  EXPECT_EQ(effective.fontPointSize, 14);
  ASSERT_TRUE(store.save(scope.capture(effective)));
  EXPECT_FALSE(Storage.exists(PATH));
}

TEST_F(BookSettings, LegacyFnvCacheIsRecognized) {
  const std::string book = "/book.epub";
  const auto old = "/.crosspoint/epub_" + std::to_string(ZipFile::fnvHash64(book.data(), book.size()));
  ASSERT_NE(old, CACHE);
  put(old + "/reader_settings.bin", crossInkV10());
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, book, settings));
  EXPECT_EQ(settings.mask, 1U << 4);
  EXPECT_EQ(settings.preferences.values[4], 1);
  EXPECT_FALSE(BookReaderPreferences::prepareMove(CACHE, book));
}

TEST_F(BookSettings, FailedLegacyCloseBlocksPreferenceWrites) {
  const std::string path = std::string(CACHE) + "/reader_settings.bin";
  put(path, crossInkV10());
  Storage.failNextCloseFor(path);
  BookReaderPreferences store;
  BookReaderSettings settings;
  EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
  EXPECT_FALSE(store.save(sizeOverride()));
  EXPECT_FALSE(Storage.exists(PATH));
}

TEST_F(BookSettings, FailedLegacyAllocationBlocksPreferenceWrites) {
  put(std::string(CACHE) + "/reader_settings.bin", crossInkV10());
  failNextTestAllocation = true;
  BookReaderPreferences store;
  BookReaderSettings settings;
  EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
  EXPECT_FALSE(store.save(sizeOverride()));
  EXPECT_FALSE(Storage.exists(PATH));
}

TEST_F(BookSettings, FailedNativeLookupCannotTriggerLegacyImport) {
  put(std::string(CACHE) + "/reader_settings.bin", crossInkV10());
  Storage.failReadsFor(PATH);
  BookReaderPreferences store;
  BookReaderSettings settings;
  EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
  EXPECT_FALSE(store.save(sizeOverride()));
}

TEST_F(BookSettings, PreservesUnknownKeys) {
  put(PATH, R"({"version":1,"future":"keep","overrides":{"fontSize":16,"futureOption":42}})");
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  ASSERT_TRUE(store.save(sizeOverride()));
  JsonDocument doc;
  ASSERT_TRUE(PersistableStoreBase::readDocFromFile(PATH, doc, 4096));
  EXPECT_STREQ(doc["future"].as<const char*>(), "keep");
  EXPECT_EQ(doc["overrides"]["futureOption"].as<int>(), 42);
}

TEST_F(BookSettings, CorruptFutureAndOversizedFilesAreNeverOverwritten) {
  for (const auto& original : {std::string("broken"), std::string(R"({"version":2,"overrides":{}})"),
                               std::string(R"({"version":1,"overrides":{"fontSize":2}})"), std::string(4097, 'x')}) {
    put(PATH, original);
    BookReaderPreferences store;
    BookReaderSettings saved;
    EXPECT_FALSE(store.load(CACHE, "/book.epub", saved));
    EXPECT_FALSE(store.save(sizeOverride()));
    std::string after;
    ASSERT_TRUE(Storage.readFileToString("TEST", PATH, 5000, after));
    EXPECT_EQ(after, original);
  }
}

TEST_F(BookSettings, FailedPublishRestoresOriginalAndRemainsRetryable) {
  put(PATH, R"({"version":1,"overrides":{"fontSize":16}})");
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  Storage.failNextRenameFrom(std::string(PATH) + ".tmp");
  EXPECT_FALSE(store.save(sizeOverride()));
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.preferences.values[0], 16);
  EXPECT_TRUE(store.save(sizeOverride()));
}

TEST_F(BookSettings, InterruptedRenameLoadsBackupBeforeTemporaryFile) {
  put(std::string(PATH) + ".bak", R"({"version":1,"overrides":{"fontSize":16}})");
  put(std::string(PATH) + ".tmp", R"({"version":1,"overrides":{"fontSize":18}})");
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.preferences.values[0], 16);
  ASSERT_TRUE(store.save(saved));
  EXPECT_TRUE(Storage.exists(PATH));
}

TEST_F(BookSettings, CacheCopyMigratesAndSurvivesClear) {
  put(std::string(CACHE) + "/reader_prefs.json", R"({"version":1,"overrides":{"fontSize":16}})");
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  ASSERT_TRUE(store.save(saved));
  ASSERT_TRUE(readingstats::clearCache(CACHE));
  EXPECT_TRUE(Storage.exists(PATH));
  EXPECT_TRUE(Storage.exists((std::string(CACHE) + "/reader_prefs.json").c_str()));
}

TEST_F(BookSettings, FinishedMovePreservesRecoveryCopyAndRollsBack) {
  put(PATH, R"({"version":1,"overrides":{"fontSize":16}})");
  ASSERT_TRUE(BookReaderPreferences::prepareMove(CACHE, "/book.epub"));
  {
    readingstats::BookMove move(CACHE, "/.crosspoint/epub_456", "/book.epub");
    ASSERT_TRUE(move.prepare());
    EXPECT_TRUE(Storage.exists("/.crosspoint/reading_stats/epub_456/reader_prefs.json"));
    EXPECT_TRUE(Storage.exists((std::string(CACHE) + "/reader_prefs.json").c_str()));
  }
  EXPECT_TRUE(Storage.exists(PATH));
}

TEST_F(BookSettings, SoleTemporaryRecoverySurvivesFailedReplacement) {
  put(std::string(PATH) + ".tmp", R"({"version":1,"overrides":{"fontSize":16}})");
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  Storage.failWritePath = std::string(PATH) + ".tmp";
  EXPECT_FALSE(store.save(sizeOverride()));
  BookReaderPreferences reopened;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.preferences.values[0], 16);
  EXPECT_TRUE(reopened.save(sizeOverride()));
}

TEST_F(BookSettings, FailedRecoveryPromotionKeepsTemporaryFile) {
  put(std::string(PATH) + ".tmp", R"({"version":1,"overrides":{"fontSize":16}})");
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  Storage.failNextRenameFrom(std::string(PATH) + ".tmp");
  EXPECT_FALSE(store.save(sizeOverride()));
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.preferences.values[0], 16);
}

TEST_F(BookSettings, FailedOutputAllocationKeepsPrimary) {
  put(PATH, R"({"version":1,"overrides":{"fontSize":16}})");
  JsonDocument document;
  document["version"] = 1;
  document["overrides"]["fontSize"] = 18;
  failNextTestAllocation = true;
  EXPECT_FALSE(PersistableStoreBase::writeDocToFileAtomic(PATH, document, 4096));
  BookReaderPreferences store;
  BookReaderSettings saved;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", saved));
  EXPECT_EQ(saved.preferences.values[0], 16);
}

TEST_F(BookSettings, FailedInputAllocationKeepsPrimary) {
  const std::string original = R"({"version":1,"overrides":{"fontSize":16}})";
  put(PATH, original);
  failNextTestAllocation = true;
  BookReaderPreferences store;
  BookReaderSettings saved;
  EXPECT_FALSE(store.load(CACHE, "/book.epub", saved));
  EXPECT_FALSE(store.save(sizeOverride()));
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", PATH, 4096, after));
  EXPECT_EQ(after, original);
}

TEST(ReaderPreferenceScope, RenderingModeIsPerBookAndRestoresGlobal) {
  CrossPointSettings settings;
  settings.epubRenderMode = 1;
  BookReaderSettingsScope scope;
  scope.begin(settings, {});
  settings.epubRenderMode = 2;
  auto book = scope.capture(settings);
  JsonDocument saved;
  book.preferences.write(saved.to<JsonObject>(), book.mask);
  EXPECT_EQ(saved["epubRenderMode"].as<int>(), 2);
  JsonDocument global;
  scope.writeGlobal(global.to<JsonObject>());
  EXPECT_EQ(global["epubRenderMode"].as<int>(), 1);
  scope.end(settings);
  EXPECT_EQ(settings.epubRenderMode, 1);
  scope.begin(settings, book);
  EXPECT_EQ(settings.epubRenderMode, 2);
  scope.reset(settings);
  EXPECT_EQ(settings.epubRenderMode, 1);
}

TEST(ReaderPreferences, RenderingModeRejectsInvalidValues) {
  JsonDocument doc;
  ASSERT_EQ(deserializeJson(doc, R"({"epubRenderMode":3})"), DeserializationError::Ok);
  ReaderPreferences snapshot;
  uint32_t mask = 0;
  EXPECT_FALSE(snapshot.read(doc.as<JsonObjectConst>(), mask));
  EXPECT_EQ(normalizeEpubRenderMode(255), EpubRenderMode::Default);
}
