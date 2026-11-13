#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include "activities/reader/BookReaderPreferences.h"
#include "activities/reader/ReadingStatsStore.h"
#include "personalization/BookReaderSettings.h"
#include "personalization/CrossInkBarImport.h"
#include "personalization/CrossInkControlImport.h"
#include "personalization/CrossInkImportGuard.h"
#include "personalization/CrossInkImportSnapshot.h"
#include "personalization/CrossInkImportTransaction.h"
#include "personalization/CrossInkLibraryImport.h"
#include "personalization/CrossInkPreferencesImport.h"
#include "personalization/CrossInkSettingsSource.h"

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

TEST_F(BookSettings, CrossInkSourceReadsPrimaryWithoutChangingFiles) {
  const std::string original = R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7,"other":"kept"})";
  put(crossinkimport::SOURCE_PATH, original);
  JsonDocument doc;
  doc["old"] = 9;
  ASSERT_EQ(crossinkimport::readSource(doc), crossinkimport::SourceResult::Loaded);
  EXPECT_TRUE(doc["old"].isUnbound());
  EXPECT_EQ(doc["sideButtonUpShort"].as<int>(), 7);
  EXPECT_STREQ(doc["other"].as<const char*>(), "kept");
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", crossinkimport::SOURCE_PATH, 32768, after));
  EXPECT_EQ(after, original);
}

TEST_F(BookSettings, CrossInkSourcePreservesOutputForInvalidOrUncertainInputs) {
  using R = crossinkimport::SourceResult;
  JsonDocument doc;
  doc["keep"] = 42;
  EXPECT_EQ(crossinkimport::readSource(doc), R::Missing);
  EXPECT_EQ(doc["keep"].as<int>(), 42);
  for (const auto& invalid : {std::string(), std::string("[]"), std::string("{bad"),
                              std::string(crossinkimport::MAX_SOURCE_BYTES + 1, ' ')}) {
    put(crossinkimport::SOURCE_PATH, invalid);
    EXPECT_EQ(crossinkimport::readSource(doc), R::Invalid);
    EXPECT_EQ(doc["keep"].as<int>(), 42);
  }
  put(crossinkimport::SOURCE_PATH, R"({"x":1})");
  Storage.failNextReadFor(crossinkimport::SOURCE_PATH);
  EXPECT_EQ(crossinkimport::readSource(doc), R::Unavailable);
  Storage.failNextCloseFor(crossinkimport::SOURCE_PATH);
  EXPECT_EQ(crossinkimport::readSource(doc), R::Unavailable);
  failNextTestAllocation = true;
  EXPECT_EQ(crossinkimport::readSource(doc), R::Unavailable);
  EXPECT_EQ(doc["keep"].as<int>(), 42);
  put(crossinkimport::BACKUP_PATH, R"({"x":2})");
  EXPECT_EQ(crossinkimport::readSource(doc), R::RecoveryPresent);
  EXPECT_EQ(doc["keep"].as<int>(), 42);
}

TEST_F(BookSettings, CrossInkSourceRefusesUncertainLookupAndDirectory) {
  using R = crossinkimport::SourceResult;
  JsonDocument doc;
  doc["keep"] = 42;
  Storage.failReadsFor(crossinkimport::TEMP_PATH);
  EXPECT_EQ(crossinkimport::readSource(doc), R::Unavailable);
  Storage.reset();
  ASSERT_TRUE(Storage.mkdir(crossinkimport::SOURCE_PATH));
  const auto result = crossinkimport::readSource(doc);
  EXPECT_TRUE(result == R::Invalid || result == R::Unavailable);
  EXPECT_EQ(doc["keep"].as<int>(), 42);
}

TEST_F(BookSettings, CrossInkSourceCanStageControlsWithoutWritingEitherFile) {
  constexpr char currentPath[] = "/.crosspoint/settings.json";
  const std::string source = R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7,
      "sideButtonDownShort":27,"quickActionsTrigger":12,"quickActionSlots":[7,8,11,15,22]})";
  const std::string native = R"({"readerUpTap":2,"pluginsEnabled":1})";
  put(crossinkimport::SOURCE_PATH, source);
  put(currentPath, native);
  JsonDocument old, staged;
  ASSERT_EQ(crossinkimport::readSource(old), crossinkimport::SourceResult::Loaded);
  ASSERT_FALSE(deserializeJson(staged, native));
  ASSERT_TRUE(crossinkimport::stageControls(old.as<JsonVariantConst>(), staged, true, false));
  EXPECT_EQ(staged["readerUpTap"].as<int>(), static_cast<int>(readershortcuts::Action::Bookmark));
  EXPECT_EQ(staged["readerDownTap"].as<int>(), static_cast<int>(readershortcuts::Action::QuickActions));
  EXPECT_EQ(staged["pluginsEnabled"].as<int>(), 1);
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", crossinkimport::SOURCE_PATH, 32768, after));
  EXPECT_EQ(after, source);
  ASSERT_TRUE(Storage.readFileToString("TEST", currentPath, 32768, after));
  EXPECT_EQ(after, native);
}

TEST_F(BookSettings, CrossInkImportSnapshotPreservesCurrentSettingsAndPriorSnapshots) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2,"pluginsEnabled":1})";
  put(primary, original);
  put("/.crosspoint/settings-crossink-00.bak", "earlier backup");
  char path[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), path));
  EXPECT_STREQ(path, "/.crosspoint/settings-crossink-01.bak");
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", path, 32768, after));
  EXPECT_EQ(after, original);
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, after));
  EXPECT_EQ(after, original);
  ASSERT_TRUE(Storage.readFileToString("TEST", "/.crosspoint/settings-crossink-00.bak", 32768, after));
  EXPECT_EQ(after, "earlier backup");
}

TEST_F(BookSettings, CrossInkImportSnapshotRefusesUncertainOrChangedSource) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})";
  put(primary, original);
  char path[crossinkimport::SNAPSHOT_PATH_SIZE] = "old";
  EXPECT_FALSE(crossinkimport::snapshotCurrentSettings("different", 9, path));
  EXPECT_STREQ(path, "");
  EXPECT_FALSE(Storage.exists("/.crosspoint/settings-crossink-00.bak"));
  put("/.crosspoint/settings.json.tmp", "partial");
  EXPECT_FALSE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), path));
  EXPECT_FALSE(Storage.exists("/.crosspoint/settings-crossink-00.bak"));
  EXPECT_TRUE(Storage.remove("/.crosspoint/settings.json.tmp"));
  Storage.failSettingsIdentity();
  EXPECT_FALSE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), path));
  EXPECT_STREQ(path, "");
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, after));
  EXPECT_EQ(after, original);
}

TEST_F(BookSettings, CrossInkImportSnapshotLeavesFailedCopiesAndRetriesAtNewName) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})";
  put(primary, original);
  Storage.failNextWriteFor("/.crosspoint/settings-crossink-00.bak", 2);
  char path[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  EXPECT_FALSE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), path));
  EXPECT_TRUE(Storage.exists("/.crosspoint/settings-crossink-00.bak"));
  EXPECT_STREQ(path, "");
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), path));
  EXPECT_STREQ(path, "/.crosspoint/settings-crossink-01.bak");
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, after));
  EXPECT_EQ(after, original);
}

TEST_F(BookSettings, CrossInkControlsTransactionPublishesAndKeepsOriginalSnapshot) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2,"pluginsEnabled":1,"custom":{"x":4}})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7})");
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  EXPECT_EQ(proposed["readerUpTap"].as<int>(), static_cast<int>(readershortcuts::Action::Bookmark));
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  EXPECT_EQ(persisted["readerUpTap"].as<int>(), static_cast<int>(readershortcuts::Action::Bookmark));
  EXPECT_EQ(persisted["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(persisted["custom"]["x"].as<int>(), 4);
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
}

TEST_F(BookSettings, CrossInkControlsTransactionPublishesTouchGesturesAndKeepsSource) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"twoFingerSwipeUp":0,"readerLeftEdgeUp":0,"pluginsEnabled":1})";
  const std::string legacy = R"({"pinchFontResizeEnabled":0,"twoFingerSwipeUp":1,"leftEdgeUp":5,"pageTurnGesture":2})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, legacy);
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved, backup, source;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, backup));
  ASSERT_TRUE(Storage.readFileToString("TEST", crossinkimport::SOURCE_PATH, 32768, source));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  EXPECT_EQ(persisted["pinchFontResizeEnabled"].as<int>(), 0);
  EXPECT_EQ(persisted["twoFingerSwipeUp"].as<int>(), 1);
  EXPECT_EQ(persisted["pageTurnGesture"].as<int>(), 2);
  EXPECT_EQ(persisted["previousPageGesture"].as<int>(), 2);
  EXPECT_EQ(persisted["readerLeftEdgeUp"].as<uint8_t>(), static_cast<uint8_t>(readershortcuts::Action::NextChapter));
  EXPECT_EQ(persisted["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(backup, original);
  EXPECT_EQ(source, legacy);
}

TEST_F(BookSettings, CrossInkLibraryImportPreservesOtherSettingsAndBacksUpOriginal) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original =
      R"({"libraryShowEpub":1,"fileBrowserDisplay":1,"libraryListExpanded":1,"pluginsEnabled":1})";
  const std::string legacy =
      R"({"libraryShowSeries":0,"libraryShowGenre":1,"libraryShowEpub":0,"libraryShowXtc":0,"libraryShowTxt":1,
    "libraryShowMarkdown":0,"libraryHideFinishedBooks":1,"hideFileExtension":1,"fileBrowserPreview":0,
    "recentBooksView":1,"uiTheme":5,"swapLibraryFileBrowser":1,"pluginsEnabled":0,
    "libraryUseMetadata":0,"showHiddenFiles":1,"removeReadBooksFromRecents":1,"moveFinishedToReadFolder":1,"libraryListExpanded":0})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, legacy);
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishLibrary(proposed, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved, backup, source;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, backup));
  ASSERT_TRUE(Storage.readFileToString("TEST", crossinkimport::SOURCE_PATH, 32768, source));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  EXPECT_EQ(persisted["libraryShowEpub"].as<int>(), 0);
  EXPECT_EQ(persisted["libraryShowSeries"].as<int>(), 0);
  EXPECT_EQ(persisted["libraryShowGenre"].as<int>(), 1);
  EXPECT_EQ(persisted["libraryShowXtc"].as<int>(), 0);
  EXPECT_EQ(persisted["libraryHideFinishedBooks"].as<int>(), 1);
  EXPECT_EQ(persisted["hideFileExtension"].as<int>(), 1);
  EXPECT_EQ(persisted["fileBrowserDisplay"].as<int>(), 0);
  EXPECT_EQ(persisted["libraryListExpanded"].as<int>(), 0);
  EXPECT_TRUE(persisted["fileBrowserPreview"].isUnbound());
  EXPECT_EQ(persisted["recentBooksView"].as<int>(), 1);
  EXPECT_EQ(persisted["uiTheme"].as<int>(), 5);
  EXPECT_EQ(persisted["libraryFirstOnHome"].as<int>(), 1);
  EXPECT_TRUE(persisted["libraryUseMetadata"].is<uint8_t>());
  EXPECT_EQ(persisted["libraryUseMetadata"].as<int>(), 0);
  EXPECT_EQ(persisted["showHiddenFiles"].as<int>(), 1);
  EXPECT_EQ(persisted["removeReadBooksFromRecents"].as<int>(), 1);
  EXPECT_EQ(persisted["moveFinishedToReadFolder"].as<int>(), 1);
  EXPECT_EQ(persisted["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(backup, original);
  EXPECT_EQ(source, legacy);
}

TEST_F(BookSettings, CrossInkLibraryStagingSkipsInvalidAndPreservesExistingWhenRequested) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"libraryShowEpub":0,"libraryShowXtc":2,
    "hideFileExtension":1,"fileBrowserDisplay":"0","recentBooksView":1})"));
  ASSERT_FALSE(deserializeJson(current, R"({"libraryShowEpub":null,"recentBooksView":0,"other":9})"));
  ASSERT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(current["libraryShowEpub"].isNull());
  EXPECT_TRUE(current["libraryShowXtc"].isUnbound());
  EXPECT_TRUE(current["fileBrowserDisplay"].isUnbound());
  EXPECT_EQ(current["recentBooksView"].as<int>(), 0);
  EXPECT_EQ(current["hideFileExtension"].as<int>(), 1);
  EXPECT_EQ(current["other"].as<int>(), 9);
}

TEST_F(BookSettings, CrossInkLibraryImportsCompatibleThemesByMeaning) {
  for (const auto [legacy, expected] : {std::pair{0, 0}, {1, 1}, {2, 2}, {3, 3}, {5, 5}, {6, 6}, {7, 4}}) {
    JsonDocument source, current;
    source["uiTheme"] = legacy;
    current.to<JsonObject>();
    ASSERT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, false));
    EXPECT_EQ(current["uiTheme"].as<int>(), expected);
  }
  JsonDocument source, current;
  source["uiTheme"] = 4;
  current["uiTheme"] = 1;
  EXPECT_FALSE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["uiTheme"].as<int>(), 1);
  source["uiTheme"] = 7;
  EXPECT_FALSE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["uiTheme"].as<int>(), 4);
}

TEST_F(BookSettings, CrossInkPreferencesImportBacksUpAndPreservesOtherSettings) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"uiScale":1,"trackReadingStats":1,"pluginsEnabled":1})";
  const std::string legacy =
      R"({"uiScale":0,"trackReadingStats":0,"readingIdleTimeThresholdUnits":12,"lineHeightPercent":145,"paragraphAlignment":3,"embeddedStyle":0,"hyphenationEnabled":1,"extraParagraphSpacing":0,"textAntiAliasing":0,"imageRendering":2,"orientation":3,"screenMarginVertical":15,"screenMarginHorizontal":15,"wordSpacing":0,"sleepScreen":4,"sleepScreenCoverMode":1,"sleepScreenCoverFilter":2,"quickResumeSleepScreen":1,"pluginsEnabled":0})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, legacy);
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishPreferences(proposed, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved, backup;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, backup));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  EXPECT_EQ(persisted["uiScale"].as<int>(), 0);
  EXPECT_EQ(persisted["trackReadingStats"].as<int>(), 0);
  EXPECT_EQ(persisted["readingIdleTimeThresholdUnits"].as<int>(), 12);
  EXPECT_EQ(persisted["lineHeight"].as<int>(), 4);
  EXPECT_EQ(persisted["paragraphAlignment"].as<int>(), 3);
  EXPECT_EQ(persisted["embeddedStyle"].as<int>(), 0);
  EXPECT_EQ(persisted["hyphenationEnabled"].as<int>(), 1);
  EXPECT_EQ(persisted["extraParagraphSpacing"].as<int>(), 0);
  EXPECT_EQ(persisted["textAntiAliasing"].as<int>(), 0);
  EXPECT_EQ(persisted["imageRendering"].as<int>(), 2);
  EXPECT_EQ(persisted["orientation"].as<int>(), 3);
  EXPECT_EQ(persisted["screenMarginHorizontal"].as<int>(), 15);
  EXPECT_EQ(persisted["wordSpacing"].as<int>(), 100);
  EXPECT_EQ(persisted["sleepScreen"].as<int>(), 5);
  EXPECT_EQ(persisted["sleepScreenCoverMode"].as<int>(), 1);
  EXPECT_EQ(persisted["sleepScreenCoverFilter"].as<int>(), 2);
  EXPECT_EQ(persisted["quickResumeSleepScreen"].as<int>(), 1);
  EXPECT_EQ(persisted["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(backup, original);
}

TEST_F(BookSettings, CrossInkPreferencesRejectInvalidAndPreserveExisting) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"uiScale":0,"trackReadingStats":2,"readingIdleTimeThresholdUnits":2})"));
  ASSERT_FALSE(deserializeJson(current, R"({"uiScale":1})"));
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["uiScale"].as<int>(), 1);
  EXPECT_TRUE(current["trackReadingStats"].isUnbound());
  EXPECT_TRUE(current["readingIdleTimeThresholdUnits"].isUnbound());
  EXPECT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["uiScale"].as<int>(), 0);
}

TEST_F(BookSettings, CrossInkPreferencesImportExactLineHeights) {
  constexpr uint8_t heights[] = {100, 110, 120, 130, 145, 160, 175};
  for (uint8_t i = 0; i < sizeof(heights); ++i) {
    JsonDocument source, current;
    source["lineHeightPercent"] = heights[i];
    current.to<JsonObject>();
    ASSERT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
    EXPECT_EQ(current["lineHeight"].as<uint8_t>(), i);
  }
  JsonDocument source, current;
  source["lineHeightPercent"] = 125;
  current.to<JsonObject>();
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_TRUE(current["lineHeight"].isUnbound());
  source["lineHeightPercent"] = 130;
  current["lineSpacing"] = 1;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(current["lineHeight"].isUnbound());
  EXPECT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["lineHeight"].as<uint8_t>(), 3);
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  source["lineHeightPercent"] = 100;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["lineHeight"].as<uint8_t>(), 3);
  source["lineHeightPercent"] = "100";
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
}

TEST_F(BookSettings, CrossInkPreferencesRejectInvalidLayoutAndPreserveCurrent) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"paragraphAlignment":5,"embeddedStyle":2,
    "hyphenationEnabled":2,"extraParagraphSpacing":2,"textAntiAliasing":2,
    "imageRendering":3,"orientation":4})"));
  ASSERT_FALSE(deserializeJson(current, R"({"paragraphAlignment":1,"embeddedStyle":1})"));
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  source["paragraphAlignment"] = 4;
  source["embeddedStyle"] = 0;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["paragraphAlignment"].as<int>(), 1);
  EXPECT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["paragraphAlignment"].as<int>(), 4);
  EXPECT_EQ(current["embeddedStyle"].as<int>(), 0);
}

TEST_F(BookSettings, CrossInkPreferencesImportIndependentMargins) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"screenMarginVertical":150,"screenMarginHorizontal":7,"wordSpacing":0})"));
  current.to<JsonObject>();
  ASSERT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["screenMarginHorizontal"].as<int>(), 7);
  EXPECT_EQ(current["screenMarginVertical"].as<int>(), 150);
  EXPECT_EQ(current["wordSpacing"].as<int>(), 100);
  source["screenMarginVertical"] = 20;
  source["screenMarginHorizontal"] = 151;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["screenMarginHorizontal"].as<int>(), 7);
  EXPECT_EQ(current["screenMarginVertical"].as<int>(), 20);
  current.clear();
  current["screenMargin"] = 10;
  current["wordSpacing"] = 100;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(current["screenMarginVertical"].isUnbound());
}

TEST_F(BookSettings, CrossInkBarsTransactionPreservesSnapshotAndUnrelatedSettings) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"pluginsEnabled":1,"custom":{"x":4}})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, R"({"readerStatusBars":{"version":1,
    "top":{"slots":[1,2,3,8,9,10,0],"percentageFormat":1,"progressBar":0,"thickness":2},
    "bottom":{"slots":[5,7,0,8,2,1,10],"percentageFormat":2,"progressBar":1,"thickness":0}}})");
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishReaderBars(proposed, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  EXPECT_TRUE(persisted["readerBarsEnabled"].as<bool>());
  EXPECT_EQ(persisted["custom"]["x"].as<int>(), 4);
  EXPECT_TRUE(persisted["readerBarsV1"]["top"]["slots"].is<JsonArrayConst>());
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
}

TEST_F(BookSettings, CrossInkBarsTransactionRefusesUnsupportedBeforeWriting) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"pluginsEnabled":1})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, R"({"readerStatusBars":{"version":1,
    "top":{"slots":[4,0,0,0,0,0,0],"percentageFormat":0,"progressBar":0,"thickness":0},
    "bottom":{"slots":[0,0,0,0,0,0,0],"percentageFormat":0,"progressBar":0,"thickness":0}}})");
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  EXPECT_EQ(crossinkimport::publishReaderBars(proposed, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Unsupported);
  EXPECT_STREQ(snapshot, "");
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  EXPECT_EQ(saved, original);
}

TEST_F(BookSettings, CrossInkFlatBarsTransactionPreservesSourceAndSnapshot) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"pluginsEnabled":1,"unrelated":42})";
  const std::string legacy = R"({"statusBarChapterPageCount":1,"statusBarBookProgressPercentage":1,
    "statusBarTitle":0,"statusBarTimeLeft":2,"statusBarBattery":1,"hideClock":0,
    "statusBarProgressBar":1,"statusBarProgressBarThickness":2,"statusBarBookPercentageFormat":2})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, legacy);
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishReaderBars(proposed, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  ReaderBar top, bottom;
  ASSERT_TRUE(readReaderBar(persisted["readerBarsV1"]["top"], top));
  ASSERT_TRUE(readReaderBar(persisted["readerBarsV1"]["bottom"], bottom));
  EXPECT_EQ(top.slots[3], ReaderBarItem::Clock);
  EXPECT_EQ(bottom.slots[0], ReaderBarItem::Battery);
  EXPECT_EQ(bottom.slots[1], ReaderBarItem::TimeLeftBook);
  EXPECT_EQ(bottom.slots[3], ReaderBarItem::BookTitle);
  EXPECT_EQ(bottom.slots[4], ReaderBarItem::ChapterPages);
  EXPECT_EQ(bottom.slots[5], ReaderBarItem::BookPercent);
  EXPECT_EQ(bottom.progress, 1);
  EXPECT_EQ(bottom.thickness, 2);
  EXPECT_EQ(bottom.percentageDecimals, 2);
  EXPECT_TRUE(persisted["readerBarsEnabled"].as<bool>());
  EXPECT_EQ(persisted["pluginsEnabled"].as<int>(), 1);
  EXPECT_EQ(persisted["unrelated"].as<int>(), 42);
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
  ASSERT_TRUE(Storage.readFileToString("TEST", crossinkimport::SOURCE_PATH, 32768, saved));
  EXPECT_EQ(saved, legacy);
}

TEST_F(BookSettings, CrossInkControlsTransactionRefusesStalePrimaryAndKeepsSnapshotOnWriteFailure) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7})");
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  EXPECT_EQ(crossinkimport::publishControls(proposed, true, false, "stale", 5, snapshot),
            crossinkimport::ApplyResult::SnapshotFailed);
  EXPECT_STREQ(snapshot, "");
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  EXPECT_EQ(saved, original);
  proposed.clear();
  ASSERT_FALSE(deserializeJson(proposed, original));
  Storage.failNextWriteFor("/.crosspoint/settings.json.tmp", 3);
  EXPECT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::WriteUncertain);
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  EXPECT_EQ(saved, original);
  EXPECT_TRUE(Storage.exists("/.crosspoint/settings.json.tmp"));
}

TEST_F(BookSettings, CrossInkControlsTransactionDoesNotWriteForNoChangesOrMissingSource) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":7})";
  put(primary, original);
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  EXPECT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::SourceMissing);
  put(crossinkimport::SOURCE_PATH, R"({"sideButtonOrientationAware":0,"sideButtonUpShort":7})");
  EXPECT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::NoChanges);
  EXPECT_STREQ(snapshot, "");
  EXPECT_FALSE(Storage.exists("/.crosspoint/settings-crossink-00.bak"));
  EXPECT_FALSE(Storage.exists("/.crosspoint/settings.json.tmp"));
}

TEST_F(BookSettings, CrossInkControlsTransactionPersistsHomeQuickActionWithBackup) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"homeButtonInReaderEnabled":1,"homeButtonTapAction":0,"powerTapAction":0})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, R"({"homeButtonInReaderEnabled":1,"homeButtonTapAction":27,
      "quickActionsTrigger":6,"quickActionSlots":[7,8,11,15,22]})");
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  JsonDocument actual;
  ASSERT_FALSE(deserializeJson(actual, saved));
  EXPECT_EQ(actual["homeButtonTapAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::QuickActions));
  EXPECT_EQ(actual["readerQuickActionsV1"].as<JsonArrayConst>().size(), 5);
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
}

TEST_F(BookSettings, CrossInkControlsTransactionImportsHomeWithImplicitDefaults) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"pluginsEnabled":1})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, R"({"homeButtonInReaderEnabled":1,"homeButtonTapAction":28})");
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishControls(proposed, true, false, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  JsonDocument actual;
  ASSERT_FALSE(deserializeJson(actual, saved));
  EXPECT_EQ(actual["homeButtonTapAction"].as<uint8_t>(), static_cast<uint8_t>(HomeButtonAction::ToggleFrontlight));
  EXPECT_EQ(actual["pluginsEnabled"].as<int>(), 1);
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
}

TEST_F(BookSettings, CrossInkImportGuardDetectsInterruptedAndCompletedImports) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})", changed = R"({"readerUpTap":7})";
  put(primary, original);
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), snapshot));
  ASSERT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::Clear);
  crossinkimport::ImportMarker marker{};
  ASSERT_TRUE(
      crossinkimport::beginImport(snapshot, original.data(), original.size(), changed.data(), changed.size(), marker));
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::NeedsReview);
  ASSERT_TRUE(crossinkimport::finishImport(marker));
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::Clear);
  EXPECT_FALSE(crossinkimport::finishImport(marker));
  put("/.crosspoint/crossink-import/.cid-00", "corrupted");
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::NeedsReview);
}

TEST_F(BookSettings, CrossInkImportGuardKeepsPartialMarkersAndFailsClosed) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})", changed = R"({"readerUpTap":7})";
  put(primary, original);
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), snapshot));
  Storage.failNextWriteFor("/.crosspoint/crossink-import/.cip-00", 4);
  crossinkimport::ImportMarker marker{};
  EXPECT_FALSE(
      crossinkimport::beginImport(snapshot, original.data(), original.size(), changed.data(), changed.size(), marker));
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::NeedsReview);
  char next[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), next));
  EXPECT_STREQ(next, "/.crosspoint/settings-crossink-01.bak");
}

TEST_F(BookSettings, CrossInkRecoveryClearsOnlyAnUnchangedPrimary) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})", changed = R"({"readerUpTap":7})";
  put(primary, original);
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), snapshot));
  crossinkimport::ImportMarker marker{};
  ASSERT_TRUE(
      crossinkimport::beginImport(snapshot, original.data(), original.size(), changed.data(), changed.size(), marker));
  put("/.crosspoint/settings.json.tmp", "partial");
  EXPECT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::NeedsReview);
  ASSERT_TRUE(Storage.remove("/.crosspoint/settings.json.tmp"));
  ASSERT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::Recovered);
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::Clear);
  EXPECT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::Recovered);
  std::string saved;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  EXPECT_EQ(saved, original);
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, saved));
  EXPECT_EQ(saved, original);
}

TEST_F(BookSettings, CrossInkRecoveryRefusesChangedPrimaryOrCorruptSnapshot) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"readerUpTap":2})", changed = R"({"readerUpTap":7})";
  put(primary, original);
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), snapshot));
  crossinkimport::ImportMarker marker{};
  ASSERT_TRUE(
      crossinkimport::beginImport(snapshot, original.data(), original.size(), changed.data(), changed.size(), marker));
  put(primary, changed);
  EXPECT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::NeedsReview);
  EXPECT_FALSE(Storage.exists("/.crosspoint/crossink-import/.cid-00"));
  put(primary, original);
  put(snapshot, "damaged");
  EXPECT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::NeedsReview);
  EXPECT_FALSE(Storage.exists("/.crosspoint/crossink-import/.cid-00"));
}

TEST_F(BookSettings, CrossInkRecoveryResumesOnlyMatchingPartialMarker) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  constexpr char done[] = "/.crosspoint/crossink-import/.cid-00";
  const std::string original = R"({"readerUpTap":2})", changed = R"({"readerUpTap":7})";
  put(primary, original);
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), snapshot));
  crossinkimport::ImportMarker marker{};
  ASSERT_TRUE(
      crossinkimport::beginImport(snapshot, original.data(), original.size(), changed.data(), changed.size(), marker));
  put(done, std::string(reinterpret_cast<const char*>(marker.data()), 10));
  ASSERT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::Recovered);
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::Clear);
}

TEST_F(BookSettings, CrossInkRecoveryPreservesConflictingPartialMarker) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  constexpr char done[] = "/.crosspoint/crossink-import/.cid-00";
  const std::string original = R"({"readerUpTap":2})", changed = R"({"readerUpTap":7})";
  put(primary, original);
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_TRUE(crossinkimport::snapshotCurrentSettings(original.data(), original.size(), snapshot));
  crossinkimport::ImportMarker marker{};
  ASSERT_TRUE(
      crossinkimport::beginImport(snapshot, original.data(), original.size(), changed.data(), changed.size(), marker));
  const std::string conflict = "user data";
  put(done, conflict);
  EXPECT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::NeedsReview);
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", done, 32768, after));
  EXPECT_EQ(after, conflict);
  EXPECT_EQ(crossinkimport::inspectImports(), crossinkimport::GuardState::NeedsReview);
  put(done, "");
  EXPECT_EQ(crossinkimport::recoverUnchangedImport(), crossinkimport::RecoveryResult::NeedsReview);
  EXPECT_TRUE(Storage.exists(done));
}

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

TEST_F(BookSettings, LegacyNaturalWordSpacingOverridesTheGlobalValue) {
  auto record = crossInkV10();
  record[record.size() - 4] = 1 << 3;
  put(std::string(CACHE) + "/reader_settings.bin", record);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  ASSERT_EQ(settings.mask, 1U << 2);
  CrossPointSettings effective;
  effective.wordSpacing = 150;
  BookReaderSettingsScope scope;
  scope.begin(effective, settings);
  EXPECT_EQ(effective.wordSpacing, 100);
  scope.end(effective);
  EXPECT_EQ(effective.wordSpacing, 150);
  ASSERT_TRUE(store.save(settings));
  EXPECT_FALSE(Storage.exists(PATH));
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

TEST_F(BookSettings, LegacySafeModeKeepsUnderlyingSettingsAndSource) {
  auto record = crossInkV10();
  record[1] |= 0x10;
  const std::string path = std::string(CACHE) + "/reader_settings.bin";
  put(path, record);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_TRUE(settings.safeMode);
  EXPECT_EQ(settings.mask, 1U << 4);
  CrossPointSettings effective;
  effective.focusReadingEnabled = 1;
  BookReaderSettingsScope scope;
  scope.begin(effective, settings);
  EXPECT_EQ(effective.embeddedStyle, 0);
  EXPECT_EQ(effective.focusReadingEnabled, 0);
  EXPECT_TRUE(scope.capture(effective).safeMode);
  ASSERT_TRUE(store.save(scope.capture(effective)));
  EXPECT_FALSE(Storage.exists(PATH));
  effective.fontPointSize = 20;
  ASSERT_TRUE(store.save(scope.capture(effective)));
  BookReaderSettings saved;
  BookReaderPreferences reopened;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", saved));
  EXPECT_TRUE(saved.safeMode);
  EXPECT_EQ(saved.preferences.values[0], 20);
  ASSERT_TRUE(reopened.save({}));
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", saved));
  EXPECT_FALSE(saved.safeMode);
  EXPECT_EQ(saved.mask, 0U);
  std::string original;
  ASSERT_TRUE(Storage.readFileToString("TEST", path, 256, original));
  EXPECT_EQ(original, record);
}

TEST_F(BookSettings, NativeSafeModeWithoutOtherOverridesPersistsAndValidates) {
  BookReaderSettings settings;
  settings.safeMode = true;
  BookReaderPreferences store;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  settings.safeMode = true;
  ASSERT_TRUE(store.save(settings));
  BookReaderSettings restored;
  BookReaderPreferences reopened;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", restored));
  EXPECT_TRUE(restored.safeMode);
  EXPECT_EQ(restored.mask, 0U);
  for (const char* value : {"null", "1", "\"true\"", "{}"}) {
    put(PATH, std::string("{\"version\":1,\"overrides\":{},\"safeMode\":") + value + "}");
    BookReaderPreferences invalid;
    EXPECT_FALSE(invalid.load(CACHE, "/book.epub", restored));
    EXPECT_FALSE(invalid.save({}));
  }
}

TEST(ReaderPreferenceScope, SafeModeRetainsUnderlyingValuesAndUnrelatedEdits) {
  CrossPointSettings settings;
  settings.focusReadingEnabled = 1;
  BookReaderSettings book;
  book.safeMode = true;
  book.preferences = ReaderPreferences::capture(settings);
  book.mask = (1U << 10) | (1U << 13);
  BookReaderSettingsScope scope;
  scope.begin(settings, book);
  EXPECT_EQ(settings.embeddedStyle, 0);
  EXPECT_EQ(settings.focusReadingEnabled, 0);
  settings.fontPointSize = 20;
  auto saved = scope.capture(settings);
  EXPECT_TRUE(saved.safeMode);
  EXPECT_EQ(saved.preferences.values[10], 1);
  EXPECT_EQ(saved.preferences.values[13], 1);
  EXPECT_EQ(saved.preferences.values[0], 20);
  EXPECT_EQ(saved.mask, book.mask | 1U);
  JsonDocument global;
  scope.writeGlobal(global.to<JsonObject>());
  EXPECT_EQ(global["embeddedStyle"].as<int>(), 1);
  EXPECT_EQ(global["focusReadingEnabled"].as<int>(), 1);
  scope.end(settings);
  EXPECT_EQ(settings.embeddedStyle, 1);
  EXPECT_EQ(settings.focusReadingEnabled, 1);
  EXPECT_EQ(settings.fontPointSize, 14);
}

TEST(ReaderPreferenceScope, SafeModeExitRestoresOnlyUneditedControls) {
  for (bool changeRenderMode : {false, true}) {
    CrossPointSettings settings;
    settings.focusReadingEnabled = 1;
    BookReaderSettings book;
    book.safeMode = true;
    book.preferences = ReaderPreferences::capture(settings);
    book.preferences.values[10] = 0;
    book.mask = 1U << 10;
    BookReaderSettingsScope scope;
    scope.begin(settings, book);
    if (changeRenderMode)
      settings.epubRenderMode = 1;
    else
      settings.embeddedStyle = 1;
    const auto saved = scope.capture(settings);
    EXPECT_FALSE(saved.safeMode);
    EXPECT_EQ(settings.embeddedStyle, changeRenderMode ? 0 : 1);
    EXPECT_EQ(settings.focusReadingEnabled, 1);
    EXPECT_EQ(saved.mask & (1U << 10), changeRenderMode ? 1U << 10 : 0U);
    EXPECT_EQ(scope.capture(settings).preferences.difference(saved.preferences), 0U);
  }
}

TEST(ReaderPreferenceScope, SafeModeUpdatesInheritedDefaultsAndResetClearsIt) {
  CrossPointSettings settings;
  BookReaderSettings book;
  book.safeMode = true;
  BookReaderSettingsScope scope;
  scope.begin(settings, book);
  scope.beforeGlobalLoad(settings);
  settings.embeddedStyle = 0;
  settings.focusReadingEnabled = 1;
  scope.afterGlobalLoad(settings);
  EXPECT_EQ(settings.embeddedStyle, 0);
  EXPECT_EQ(settings.focusReadingEnabled, 0);
  const auto saved = scope.capture(settings);
  EXPECT_TRUE(saved.safeMode);
  EXPECT_EQ(saved.mask, 0U);
  EXPECT_EQ(saved.preferences.values[10], 0);
  EXPECT_EQ(saved.preferences.values[13], 1);
  scope.reset(settings);
  EXPECT_FALSE(scope.capture(settings).safeMode);
  EXPECT_EQ(settings.embeddedStyle, 0);
  EXPECT_EQ(settings.focusReadingEnabled, 1);
}

TEST(ReaderPreferenceScope, SafeModeOwnershipAndExitMatrix) {
  for (unsigned owned = 0; owned < 4; ++owned) {
    for (unsigned choices = 0; choices < 4; ++choices) {
      for (unsigned edit = 1; edit < 8; ++edit) {
        CrossPointSettings settings;
        settings.focusReadingEnabled = 1;
        BookReaderSettings book;
        book.safeMode = true;
        book.preferences = ReaderPreferences::capture(settings);
        book.preferences.values[10] = choices & 1;
        book.preferences.values[13] = (choices >> 1) & 1;
        book.mask = ((owned & 1) ? 1U << 10 : 0U) | ((owned & 2) ? 1U << 13 : 0U);
        const unsigned underlyingStyle = (owned & 1) ? book.preferences.values[10] : 1;
        const unsigned underlyingFocus = (owned & 2) ? book.preferences.values[13] : 1;
        BookReaderSettingsScope scope;
        scope.begin(settings, book);
        if (edit & 1) settings.embeddedStyle = 1;
        if (edit & 2) settings.focusReadingEnabled = 1;
        if (edit & 4) settings.epubRenderMode = 1;
        const auto saved = scope.capture(settings);
        EXPECT_FALSE(saved.safeMode);
        EXPECT_EQ(settings.embeddedStyle, (edit & 1) ? 1 : underlyingStyle);
        EXPECT_EQ(settings.focusReadingEnabled, (edit & 2) ? 1 : underlyingFocus);
        uint32_t expectedMask = book.mask;
        if (edit & 1) expectedMask &= ~(1U << 10);
        if (edit & 2) expectedMask &= ~(1U << 13);
        if (edit & 4) expectedMask |= 1U << 14;
        EXPECT_EQ(saved.mask, expectedMask);
        EXPECT_EQ(saved.preferences.values[10], settings.embeddedStyle);
        EXPECT_EQ(saved.preferences.values[13], settings.focusReadingEnabled);
      }
    }
  }
}

TEST(ReaderPreferenceScope, SafeModeHonorsIndependentRenderProfile) {
  for (uint8_t profile = 0; profile < EPUB_RENDER_MODE_COUNT; ++profile) {
    CrossPointSettings settings;
    BookReaderSettings book;
    book.safeMode = true;
    book.mask = 1U << 14;
    book.preferences.values[14] = profile;
    BookReaderSettingsScope scope;
    scope.begin(settings, book);
    EXPECT_EQ(settings.epubRenderMode, profile);
    EXPECT_EQ(scope.capture(settings).preferences.values[14], profile);
    EXPECT_TRUE(scope.capture(settings).safeMode);
    settings.epubRenderMode = (profile + 1) % EPUB_RENDER_MODE_COUNT;
    EXPECT_FALSE(scope.capture(settings).safeMode);
    EXPECT_EQ(settings.embeddedStyle, 1);
  }
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

TEST_F(BookSettings, LegacyNamedFontLoadsWithSizeAndPreservesOriginalUntilEdited) {
  auto record = crossInkV10();
  record.replace(24, 8, "Literata");
  record[record.size() - 2] = 4;  // v10 font-name override at bit 18.
  const std::string legacyPath = std::string(CACHE) + "/reader_settings.bin";
  put(legacyPath, record);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, ReaderPreferences::FONT_BIT | 1U | (1U << 4));
  EXPECT_STREQ(settings.preferences.font, "Literata");
  EXPECT_EQ(settings.preferences.values[0], 18);
  CrossPointSettings effective;
  BookReaderSettingsScope scope;
  scope.begin(effective, settings);
  EXPECT_STREQ(effective.sdFontFamilyName, "Literata");
  EXPECT_EQ(effective.fontPointSize, 18);
  ASSERT_TRUE(store.save(scope.capture(effective)));
  EXPECT_FALSE(Storage.exists(PATH));
  effective.lineSpacing = 1;
  ASSERT_TRUE(store.save(scope.capture(effective)));
  ASSERT_TRUE(Storage.exists(PATH));
  BookReaderPreferences reopened;
  BookReaderSettings restored;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", restored));
  EXPECT_STREQ(restored.preferences.font, "Literata");
  EXPECT_EQ(restored.preferences.values[0], 18);
  std::string original;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacyPath, 256, original));
  EXPECT_EQ(original, record);
  scope.end(effective);
  EXPECT_STREQ(effective.sdFontFamilyName, "");
  EXPECT_EQ(effective.fontPointSize, 14);
}

TEST_F(BookSettings, LegacyFontWithoutOwnershipOrCompatibleNameKeepsCurrentFontAndSize) {
  for (const auto& name : {std::string(""), std::string(32, 'a'), std::string("bad/name"), std::string(".hidden")}) {
    Storage.reset();
    auto record = crossInkV10();
    record.replace(24, name.size(), name);
    record[record.size() - 2] = 4;
    put(std::string(CACHE) + "/reader_settings.bin", record);
    BookReaderPreferences store;
    BookReaderSettings settings;
    ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
    EXPECT_EQ(settings.mask, 1U << 4);
  }
  auto record = crossInkV10();
  record.replace(24, 8, "Literata");
  put(std::string(CACHE) + "/reader_settings.bin", record);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, 1U << 4);
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

TEST_F(BookSettings, CrossInkHomeOrderImportsOnlyWhenOldThemeUsedIt) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"uiTheme":5,"swapLibraryFileBrowser":1})"));
  ASSERT_FALSE(deserializeJson(current, R"({"libraryFirstOnHome":0,"uiTheme":2})"));
  ASSERT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["libraryFirstOnHome"].as<int>(), 1);
  EXPECT_EQ(current["uiTheme"].as<int>(), 5);
  source["uiTheme"] = 6;
  source["swapLibraryFileBrowser"] = 0;
  ASSERT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["libraryFirstOnHome"].as<int>(), 0);
  source["uiTheme"] = 1;
  source["swapLibraryFileBrowser"] = 1;
  EXPECT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["libraryFirstOnHome"].as<int>(), 0);
  source.remove("uiTheme");
  EXPECT_FALSE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  source["uiTheme"] = 5;
  source["swapLibraryFileBrowser"] = 2;
  EXPECT_TRUE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["libraryFirstOnHome"].as<int>(), 0);
  source["swapLibraryFileBrowser"] = 1;
  EXPECT_FALSE(crossinkimport::stageLibrary(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["libraryFirstOnHome"].as<int>(), 0);
}

TEST_F(BookSettings, RcPerBookSettingsImportExactFieldsAndPreserveSource) {
  auto source = crossInkV10();
  source[0] = 13;
  const uint32_t mask = (1U << 4) | (1U << 12) | (1U << 18) | (1U << 20) | (1U << 21);
  for (unsigned i = 0; i < 4; ++i) source[source.size() - 4 + i] = static_cast<char>(mask >> (8 * i));
  source.push_back(0);  // B/W image rendering
  source.push_back(7);  // +1 px character spacing
  source.push_back(2);  // Publisher fonts stay in the source, unsupported by this import.
  const std::string legacy = std::string(CACHE) + "/reader_settings.bin";
  put(legacy, source);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, (1U << 4) | (1U << 12) | (1U << 3));
  EXPECT_EQ(settings.preferences.values[4], 1);
  EXPECT_EQ(settings.preferences.values[12], 3);
  EXPECT_EQ(settings.preferences.values[3], 3);
  ASSERT_TRUE(store.save(settings));
  EXPECT_FALSE(Storage.exists(PATH));  // Opening alone does not create a native override file.
  settings.preferences.values[4] = 2;
  ASSERT_TRUE(store.save(settings));
  EXPECT_TRUE(Storage.exists(PATH));
  BookReaderPreferences reopened;
  BookReaderSettings loaded;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", loaded));
  EXPECT_EQ(loaded.preferences.values[4], 2);
  EXPECT_EQ(loaded.mask, settings.mask);
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 256, after));
  EXPECT_EQ(after, source);
}

TEST_F(BookSettings, CrossInkPreferencesMapWholePixelCharacterSpacing) {
  for (unsigned raw = 0; raw <= 11; ++raw) {
    JsonDocument source, current;
    source["characterSpacing"] = raw;
    current.to<JsonObject>();
    const bool exact = raw >= 1 && raw <= 9 && raw % 2 == 1;
    EXPECT_EQ(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false), exact);
    if (exact)
      EXPECT_EQ(current["characterSpacing"].as<unsigned>(), (raw - 1) / 2);
    else
      EXPECT_TRUE(current["characterSpacing"].isUnbound());
  }
}

TEST_F(BookSettings, CrossInkPreferencesCombineImageDisplayAndGrayscale) {
  for (unsigned mode = 0; mode <= 2; ++mode) {
    for (unsigned gray = 0; gray <= 1; ++gray) {
      JsonDocument source, current;
      source["imageRendering"] = mode;
      source["imageGrayscale"] = gray;
      current.to<JsonObject>();
      ASSERT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
      EXPECT_EQ(current["imageRendering"].as<unsigned>(), mode == 0 && gray == 0 ? 3 : mode);
      EXPECT_TRUE(current["imageGrayscale"].isUnbound());
    }
  }
}

TEST_F(BookSettings, CrossInkPreferencesPreserveCurrentAndRejectInvalidMappedValues) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(
      source,
      R"({"characterSpacing":9,"imageRendering":0,"imageGrayscale":0,"frontlightBrightness":80,"frontlightWarmth":40,"frontlightOn":1})"));
  ASSERT_FALSE(deserializeJson(
      current,
      R"({"characterSpacing":2,"imageRendering":1,"frontlightBrightness":50,"frontlightWarmth":20,"frontlightOn":0})"));
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["characterSpacing"].as<int>(), 4);
  EXPECT_EQ(current["imageRendering"].as<int>(), 3);
  EXPECT_EQ(current["frontlightBrightness"].as<int>(), 80);
  EXPECT_EQ(current["frontlightWarmth"].as<int>(), 40);
  EXPECT_EQ(current["frontlightOn"].as<int>(), 1);
  source.clear();
  current.clear();
  current.to<JsonObject>();
  ASSERT_FALSE(deserializeJson(
      source,
      R"({"imageRendering":0,"imageGrayscale":2,"frontlightBrightness":101,"frontlightWarmth":-1,"frontlightOn":2,"characterSpacing":-1})"));
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current.size(), 0U);
}

TEST_F(BookSettings, CrossInkControlsImportHomeEnableWithoutRemovingQuickOwner) {
  JsonDocument source, current;
  source["homeButtonInReaderEnabled"] = 0;
  current.to<JsonObject>();
  ASSERT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, false, false));
  EXPECT_EQ(current["homeButtonInReaderEnabled"].as<int>(), 0);
  source["homeButtonInReaderEnabled"] = 1;
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, false, false));
  EXPECT_TRUE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, true, false));
  EXPECT_EQ(current["homeButtonInReaderEnabled"].as<int>(), 1);
  current["homeButtonTapAction"] = static_cast<uint8_t>(HomeButtonAction::QuickActions);
  source["homeButtonInReaderEnabled"] = 0;
  EXPECT_FALSE(crossinkimport::stageControls(source.as<JsonVariantConst>(), current, true, true));
  EXPECT_EQ(current["homeButtonInReaderEnabled"].as<int>(), 1);
}

TEST_F(BookSettings, CrossInkPreferencesPublishNewMappingsAndPreserveSource) {
  constexpr char primary[] = "/.crosspoint/settings.json";
  const std::string original = R"({"pluginsEnabled":1,"frontlightBrightness":50})";
  const std::string legacy =
      R"({"characterSpacing":9,"imageRendering":0,"imageGrayscale":0,"frontlightBrightness":85,"frontlightWarmth":35,"frontlightOn":1,"pluginsEnabled":0,"sdFontFamilyName":"Literata","fontSize":21,"readerDarkMode":1,"sleepTimeout":4})";
  put(primary, original);
  put(crossinkimport::SOURCE_PATH, legacy);
  JsonDocument proposed;
  ASSERT_FALSE(deserializeJson(proposed, original));
  char snapshot[crossinkimport::SNAPSHOT_PATH_SIZE]{};
  ASSERT_EQ(crossinkimport::publishPreferences(proposed, original.data(), original.size(), snapshot),
            crossinkimport::ApplyResult::Published);
  std::string saved, backup, source;
  ASSERT_TRUE(Storage.readFileToString("TEST", primary, 32768, saved));
  ASSERT_TRUE(Storage.readFileToString("TEST", snapshot, 32768, backup));
  ASSERT_TRUE(Storage.readFileToString("TEST", crossinkimport::SOURCE_PATH, 32768, source));
  JsonDocument persisted;
  ASSERT_FALSE(deserializeJson(persisted, saved));
  EXPECT_EQ(persisted["characterSpacing"].as<int>(), 4);
  EXPECT_EQ(persisted["imageRendering"].as<int>(), 3);
  EXPECT_EQ(persisted["frontlightBrightness"].as<int>(), 85);
  EXPECT_EQ(persisted["frontlightWarmth"].as<int>(), 35);
  EXPECT_EQ(persisted["frontlightOn"].as<int>(), 1);
  EXPECT_EQ(persisted["screenInverted"].as<int>(), 1);
  EXPECT_EQ(persisted["sleepTimeoutMinutes"].as<int>(), 30);
  EXPECT_TRUE(persisted["readerDarkMode"].isUnbound());
  EXPECT_TRUE(persisted["sleepTimeout"].isUnbound());
  EXPECT_EQ(persisted["pluginsEnabled"].as<int>(), 1);
  EXPECT_STREQ(persisted["sdFontFamilyName"].as<const char*>(), "Literata");
  EXPECT_EQ(persisted["fontSize"].as<int>(), 21);
  EXPECT_EQ(backup, original);
  EXPECT_EQ(source, legacy);
}

TEST(ReaderPreferenceScope, DictionaryChoiceIsPinnedWithoutLeakingIntoGlobalSettings) {
  CrossPointSettings settings;
  strcpy(settings.dictionaryName, "English");
  BookReaderSettingsScope scope;
  scope.begin(settings, {});
  ASSERT_TRUE(scope.setDictionary(settings, "French"));
  auto saved = scope.capture(settings);
  EXPECT_EQ(saved.mask, ReaderPreferences::DICTIONARY_BIT);
  EXPECT_STREQ(saved.preferences.dictionary, "French");
  JsonDocument global;
  scope.writeGlobal(global.to<JsonObject>());
  EXPECT_STREQ(global["dictionaryName"].as<const char*>(), "English");
  scope.end(settings);
  EXPECT_STREQ(settings.dictionaryName, "English");
  scope.begin(settings, saved);
  EXPECT_STREQ(settings.dictionaryName, "French");
  ASSERT_TRUE(scope.setDictionary(settings, ""));
  EXPECT_NE(scope.capture(settings).mask & ReaderPreferences::DICTIONARY_BIT, 0U);
  scope.beforeGlobalLoad(settings);
  strcpy(settings.dictionaryName, "German");
  scope.afterGlobalLoad(settings);
  EXPECT_STREQ(settings.dictionaryName, "");
  ASSERT_TRUE(scope.setDictionary(settings, nullptr));
  EXPECT_STREQ(settings.dictionaryName, "German");
  EXPECT_EQ(scope.capture(settings).mask & ReaderPreferences::DICTIONARY_BIT, 0U);
}

TEST(ReaderPreferenceScope, MatchingDictionaryChoiceRemainsExplicit) {
  CrossPointSettings settings;
  strcpy(settings.dictionaryName, "English");
  BookReaderSettingsScope scope;
  scope.begin(settings, {});
  ASSERT_TRUE(scope.setDictionary(settings, "English"));
  EXPECT_NE(scope.capture(settings).mask & ReaderPreferences::DICTIONARY_BIT, 0U);
  for (const auto& name : {std::string(32, 'a'), std::string("../English"), std::string("a/b"), std::string("a\\b")}) {
    EXPECT_FALSE(scope.setDictionary(settings, name.c_str()));
    EXPECT_STREQ(settings.dictionaryName, "English");
  }
  scope.end(settings);
  EXPECT_FALSE(scope.setDictionary(settings, "French"));
}

TEST_F(BookSettings, DictionaryOverrideAndExplicitNoneRoundTrip) {
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  settings.mask = ReaderPreferences::DICTIONARY_BIT;
  strcpy(settings.preferences.dictionary, "English");
  ASSERT_TRUE(store.save(settings));
  BookReaderPreferences reopened;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", settings));
  EXPECT_STREQ(settings.preferences.dictionary, "English");
  settings.preferences.dictionary[0] = '\0';
  ASSERT_TRUE(reopened.save(settings));
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, ReaderPreferences::DICTIONARY_BIT);
  EXPECT_STREQ(settings.preferences.dictionary, "");
}

TEST(ReaderPreferences, DictionaryRejectsMalformedNamesAndKeepsInheritanceDistinct) {
  for (const char* json :
       {"{\"dictionaryName\":123}", "{\"dictionaryName\":\"../x\"}", "{\"dictionaryName\":\"a/b\"}",
        "{\"dictionaryName\":\"a\\u0000b\"}", "{\"dictionaryName\":\"12345678901234567890123456789012\"}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    ReaderPreferences result;
    uint32_t mask = 0;
    EXPECT_FALSE(result.read(doc.as<JsonObjectConst>(), mask));
  }
  JsonDocument doc;
  doc["dictionaryName"] = nullptr;
  ReaderPreferences result;
  uint32_t mask = 1;
  ASSERT_TRUE(result.read(doc.as<JsonObjectConst>(), mask));
  EXPECT_EQ(mask, 0U);
  result.write(doc.as<JsonObject>(), 0);
  EXPECT_FALSE(doc["dictionaryName"].isUnbound());
  EXPECT_TRUE(doc["dictionaryName"].isNull());
}

TEST_F(BookSettings, ImportsLegacyDictionaryAndRemembersExplicitInheritance) {
  const auto legacy = std::string(CACHE) + "/dictionary.bin";
  put(legacy, "/dictionaries/English/en");
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, ReaderPreferences::DICTIONARY_BIT);
  EXPECT_STREQ(settings.preferences.dictionary, "English");
  ASSERT_TRUE(store.save(settings));
  EXPECT_FALSE(Storage.exists(PATH));
  settings.mask = 0;
  ASSERT_TRUE(store.save(settings));
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, 0U);
  std::string bytes;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 128, bytes));
  EXPECT_EQ(bytes, "/dictionaries/English/en");
}

TEST_F(BookSettings, ImportsDictionaryAlongsideExistingLayoutPreferences) {
  put(PATH, "{\"version\":1,\"overrides\":{\"fontSize\":18}}");
  put(std::string(CACHE) + "/dictionary.bin", "/.dictionaries/French/");
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.preferences.values[0], 18U);
  EXPECT_STREQ(settings.preferences.dictionary, "French");
  EXPECT_NE(settings.mask & ReaderPreferences::DICTIONARY_BIT, 0U);
  ASSERT_TRUE(store.save(settings));
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_STREQ(settings.preferences.dictionary, "French");
}

TEST_F(BookSettings, UnsupportedLegacyDictionaryDoesNotGetOverwritten) {
  for (const auto& value :
       {std::string("/elsewhere/English"), std::string("/dictionaries/a/b/c"), std::string(128, 'x')}) {
    Storage.reset();
    const auto legacy = std::string(CACHE) + "/dictionary.bin";
    put(legacy, value);
    BookReaderPreferences store;
    BookReaderSettings settings;
    EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
    EXPECT_FALSE(store.save({}));
    std::string bytes;
    ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 256, bytes));
    EXPECT_EQ(bytes, value);
  }
}

TEST_F(BookSettings, AmbiguousLegacyDictionaryRootsRemainUntouched) {
  const auto legacy = std::string(CACHE) + "/dictionary.bin";
  put(legacy, "/.dictionaries/English/en");
  ASSERT_TRUE(Storage.mkdir("/dictionaries/English"));
  BookReaderPreferences store;
  BookReaderSettings settings;
  EXPECT_FALSE(store.load(CACHE, "/book.epub", settings));
  EXPECT_FALSE(store.save({}));
  std::string bytes;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 128, bytes));
  EXPECT_EQ(bytes, "/.dictionaries/English/en");
}

TEST(ReaderPreferenceScope, IndexingChoiceStaysWithBookAndRestoresGlobal) {
  CrossPointSettings settings;
  BookReaderSettingsScope scope;
  scope.begin(settings, {});
  settings.indexingMethod = CrossPointSettings::INDEXING_FULL_SECTION;
  const auto book = scope.capture(settings);
  EXPECT_NE(book.mask & ReaderPreferences::INDEXING_BIT, 0U);
  JsonDocument global;
  scope.writeGlobal(global.to<JsonObject>());
  EXPECT_EQ(global["indexingMethod"].as<unsigned>(), 0U);
  scope.end(settings);
  EXPECT_EQ(settings.indexingMethod, CrossPointSettings::INDEXING_INCREMENTAL);
  scope.begin(settings, book);
  EXPECT_EQ(settings.indexingMethod, CrossPointSettings::INDEXING_FULL_SECTION);
  scope.reset(settings);
  EXPECT_EQ(settings.indexingMethod, CrossPointSettings::INDEXING_INCREMENTAL);
}

TEST_F(BookSettings, LegacyIndexingChoiceMapsWithoutChangingOriginal) {
  auto original = crossInkV10();
  original[23] = 1;
  original[original.size() - 4] = 0;
  original[original.size() - 2] = 2;  // Override bit 17.
  const auto legacy = std::string(CACHE) + "/reader_settings.bin";
  put(legacy, original);
  BookReaderPreferences store;
  BookReaderSettings settings;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", settings));
  EXPECT_EQ(settings.mask, ReaderPreferences::INDEXING_BIT);
  EXPECT_EQ(settings.preferences.indexingMethod, 1U);
  ASSERT_TRUE(store.save(settings));
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 256, after));
  EXPECT_EQ(after, original);
}

TEST(ReaderPreferences, InvalidIndexingModesAreRejected) {
  for (const char* json : {"{\"indexingMethod\":2}", "{\"indexingMethod\":-1}", "{\"indexingMethod\":\"full\"}"}) {
    JsonDocument doc;
    ASSERT_FALSE(deserializeJson(doc, json));
    ReaderPreferences prefs;
    uint32_t mask = 0;
    EXPECT_FALSE(prefs.read(doc.as<JsonObjectConst>(), mask));
  }
}

TEST_F(BookSettings, LegacyRenderProfileIsImportedAndResetWithoutChangingOriginal) {
  auto original = crossInkV10();
  original[1] = 4;  // The render-profile override is separate from the layout snapshot.
  original[4] = 2;
  const auto legacy = std::string(CACHE) + "/reader_settings.bin";
  put(legacy, original);
  BookReaderPreferences store;
  BookReaderSettings imported;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", imported));
  ASSERT_EQ(imported.mask, 1U << 14);
  EXPECT_EQ(imported.preferences.values[14], 2U);
  CrossPointSettings settings;
  BookReaderSettingsScope scope;
  scope.begin(settings, imported);
  EXPECT_EQ(settings.epubRenderMode, 2U);
  ASSERT_TRUE(store.save(imported));
  EXPECT_FALSE(Storage.exists(PATH));
  scope.reset(settings);
  EXPECT_EQ(settings.epubRenderMode, 0U);
  ASSERT_TRUE(store.save(scope.capture(settings)));
  BookReaderPreferences reopened;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", imported));
  EXPECT_EQ(imported.mask, 0U);
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 256, after));
  EXPECT_EQ(after, original);
}

TEST(ReaderPreferences, OldCommonMarginMigratesToBothAxesAndCanonicalKeysWin) {
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"screenMargin":15,"screenMarginHorizontal":7})"));
  ReaderPreferences preferences;
  uint32_t mask;
  ASSERT_TRUE(preferences.read(doc.as<JsonObjectConst>(), mask));
  CrossPointSettings settings;
  preferences.apply(settings, mask);
  EXPECT_EQ(settings.screenMargin, 7);
  EXPECT_EQ(settings.screenMarginVertical, 15);
  preferences.write(doc.as<JsonObject>(), mask);
  EXPECT_TRUE(doc["screenMargin"].isUnbound());
  EXPECT_EQ(doc["screenMarginHorizontal"].as<int>(), 7);
  EXPECT_EQ(doc["screenMarginVertical"].as<int>(), 15);
}

TEST(ReaderPreferences, IndependentMarginValidation) {
  for (const char* key : {"screenMarginHorizontal", "screenMarginVertical"}) {
    for (int value : {-1, 0, 4, 5, 7, 40, 41, 150, 151, 256}) {
      JsonDocument doc;
      doc[key] = value;
      ReaderPreferences preferences;
      uint32_t mask;
      EXPECT_EQ(preferences.read(doc.as<JsonObjectConst>(), mask), value >= 5 && value <= 150);
    }
  }
}

TEST(ReaderPreferenceScope, MarginAxesRemainIndependentAcrossBookAndGlobalEdits) {
  CrossPointSettings settings;
  BookReaderSettingsScope scope;
  scope.begin(settings, {});
  settings.screenMargin = 7;
  auto book = scope.capture(settings);
  EXPECT_EQ(book.mask, 1U << 5);
  scope.beforeGlobalLoad(settings);
  settings.screenMargin = 25;
  settings.screenMarginVertical = 33;
  scope.afterGlobalLoad(settings);
  EXPECT_EQ(settings.screenMargin, 7);
  EXPECT_EQ(settings.screenMarginVertical, 33);
  settings.screenMarginVertical = 150;
  book = scope.capture(settings);
  EXPECT_EQ(book.mask, (1U << 5) | ReaderPreferences::VERTICAL_MARGIN_BIT);
  JsonDocument defaults;
  scope.writeGlobal(defaults.to<JsonObject>());
  EXPECT_EQ(defaults["screenMarginHorizontal"].as<int>(), 25);
  EXPECT_EQ(defaults["screenMarginVertical"].as<int>(), 33);
  scope.end(settings);
  EXPECT_EQ(settings.screenMargin, 25);
  EXPECT_EQ(settings.screenMarginVertical, 33);
  scope.begin(settings, book);
  EXPECT_EQ(settings.screenMargin, 7);
  EXPECT_EQ(settings.screenMarginVertical, 150);
  scope.reset(settings);
  EXPECT_EQ(settings.screenMargin, 25);
  EXPECT_EQ(settings.screenMarginVertical, 33);
  EXPECT_EQ(scope.capture(settings).mask, 0U);
}

TEST_F(BookSettings, IndependentLegacyMarginsSurviveNativeSaveWithoutChangingSource) {
  auto original = crossInkV10();
  original[10] = static_cast<char>(150);
  original[11] = 7;
  original[original.size() - 4] = 0x60;
  const auto legacy = std::string(CACHE) + "/reader_settings.bin";
  put(legacy, original);
  BookReaderPreferences store;
  BookReaderSettings imported;
  ASSERT_TRUE(store.load(CACHE, "/book.epub", imported));
  EXPECT_EQ(imported.mask, (1U << 5) | ReaderPreferences::VERTICAL_MARGIN_BIT);
  EXPECT_EQ(imported.preferences.values[5], 7);
  EXPECT_EQ(imported.preferences.verticalMargin, 150);
  ASSERT_TRUE(store.save(imported));
  EXPECT_FALSE(Storage.exists(PATH));
  imported.preferences.verticalMargin = 33;
  ASSERT_TRUE(store.save(imported));
  BookReaderPreferences reopened;
  BookReaderSettings loaded;
  ASSERT_TRUE(reopened.load(CACHE, "/book.epub", loaded));
  EXPECT_EQ(loaded.mask, imported.mask);
  EXPECT_EQ(loaded.preferences.values[5], 7);
  EXPECT_EQ(loaded.preferences.verticalMargin, 33);
  std::string after;
  ASSERT_TRUE(Storage.readFileToString("TEST", legacy, 256, after));
  EXPECT_EQ(after, original);
}
