#include <CrossPointSettings.h>
#include <HalStorage.h>
#include <Memory.h>
#include <PersistableStore.h>
#include <ZipFile.h>
#include <gtest/gtest.h>

#include "activities/reader/BookReaderPreferences.h"
#include "activities/reader/ReadingStatsStore.h"
#include "personalization/BookReaderSettings.h"
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
  const std::string original = R"({"libraryShowEpub":1,"fileBrowserDisplay":1,"pluginsEnabled":1})";
  const std::string legacy =
      R"({"libraryShowSeries":0,"libraryShowGenre":1,"libraryShowEpub":0,"libraryShowXtc":0,"libraryShowTxt":1,
    "libraryShowMarkdown":0,"libraryHideFinishedBooks":1,"hideFileExtension":1,"fileBrowserDisplay":0,
    "recentBooksView":1,"uiTheme":5,"swapLibraryFileBrowser":1,"pluginsEnabled":0})";
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
  EXPECT_EQ(persisted["recentBooksView"].as<int>(), 1);
  EXPECT_EQ(persisted["uiTheme"].as<int>(), 5);
  EXPECT_EQ(persisted["libraryFirstOnHome"].as<int>(), 1);
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
  EXPECT_EQ(persisted["screenMargin"].as<int>(), 15);
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

TEST_F(BookSettings, CrossInkPreferencesImportMatchingMarginsAndDefaultWordSpacing) {
  JsonDocument source, current;
  ASSERT_FALSE(deserializeJson(source, R"({"screenMarginVertical":15,"screenMarginHorizontal":15,"wordSpacing":0})"));
  current.to<JsonObject>();
  ASSERT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["screenMargin"].as<uint8_t>(), 15);
  EXPECT_EQ(current["wordSpacing"].as<uint8_t>(), 100);
  source["screenMarginVertical"] = 20;
  source["wordSpacing"] = 1;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  source["screenMarginHorizontal"] = 41;
  source["screenMarginVertical"] = 41;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  source["screenMarginHorizontal"] = 20;
  source["screenMarginVertical"] = 20;
  source["wordSpacing"] = 0;
  EXPECT_FALSE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, false));
  EXPECT_EQ(current["screenMargin"].as<uint8_t>(), 15);
  EXPECT_TRUE(crossinkimport::stagePreferences(source.as<JsonVariantConst>(), current, true));
  EXPECT_EQ(current["screenMargin"].as<uint8_t>(), 20);
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
