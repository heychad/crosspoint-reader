#include "CrossPointSettings.h"

#include <BlePageTurnerJson.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>
#include <SettingsRecoveryProbe.h>
#include <SettingsSafeWrite.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

#include "I18nKeys.h"
#include "ReaderFontSizes.h"
#include "ReaderLineSpacing.h"
#include "SettingsList.h"
#include "activities/reader/StatsSaveNotice.h"
#include "fontIds.h"
#include "personalization/CrossInkImportGuard.h"
#include "personalization/LegacyHomeShortcut.h"
#include "personalization/LegacyPowerShortcuts.h"
#include "personalization/LegacySideShortcuts.h"
#include "personalization/ReaderBarsJson.h"
#include "personalization/ReaderGestureJson.h"
#include "personalization/ReaderQuickActions.h"
#include "personalization/ReaderQuickBindings.h"
#include "util/ParagraphIndentMigration.h"
#include "util/PluginLocations.h"

namespace {

// Stack buffer for "<key>_obf" key construction — avoids a std::string
// allocation per obfuscated setting on every save and load.
constexpr size_t OBF_KEY_BUF = 64;

// Null-terminated copy into a fixed-size settings field.
void copyToField(char* dest, const char* src, const size_t maxLen) {
  strncpy(dest, src, maxLen - 1);
  dest[maxLen - 1] = '\0';
}

}  // namespace

bool CrossPointSettings::saveSpecializedToFile() const {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (!crossInkImportGuardChecked) {
    crossInkImportGuardBlocked = crossinkimport::inspectImports() != crossinkimport::GuardState::Clear;
    crossInkImportGuardChecked = true;
  }
  if (crossInkImportWriteBlocked || crossInkImportGuardBlocked) {
    LOG_ERR("CPS", "Settings writes blocked after an uncertain CrossInk import");
    settingsSaveNotice().recordFailure();
    return false;
  }
  const bool saved = [this] {
    size_t size = 0;
    std::unique_ptr<char[]> bytes;
    {
      JsonDocument doc;
      toJson(doc);
      size = measureJson(doc);
      if (doc.overflowed() || size == 0 || size > 32768) return false;
      bytes = makeUniqueNoThrow<char[]>(size + 1);
      if (!bytes || serializeJson(doc, bytes.get(), size + 1) != size) return false;
    }
    const auto directory = Storage.lookupPath("/.crosspoint");
    if (directory == HalStorage::PathStatus::Failed ||
        (directory == HalStorage::PathStatus::Missing && !Storage.mkdir("/.crosspoint")))
      return false;
    return settingsrecovery::writeSettings(bytes.get(), size);
  }();
  if (!saved) settingsSaveNotice().recordFailure();
  return saved;
}

crossinkimport::ApplyResult CrossPointSettings::importCrossInkControls(
    char (&snapshot)[crossinkimport::SNAPSHOT_PATH_SIZE]) {
  return importCrossInkSettings(crossinkimport::ImportKind::Controls, snapshot);
}

crossinkimport::ApplyResult CrossPointSettings::importCrossInkBars(
    char (&snapshot)[crossinkimport::SNAPSHOT_PATH_SIZE]) {
  return importCrossInkSettings(crossinkimport::ImportKind::ReaderBars, snapshot);
}
crossinkimport::ApplyResult CrossPointSettings::importCrossInkLibrary(
    char (&snapshot)[crossinkimport::SNAPSHOT_PATH_SIZE]) {
  return importCrossInkSettings(crossinkimport::ImportKind::Library, snapshot);
}
crossinkimport::ApplyResult CrossPointSettings::importCrossInkPreferences(
    char (&snapshot)[crossinkimport::SNAPSHOT_PATH_SIZE]) {
  return importCrossInkSettings(crossinkimport::ImportKind::Preferences, snapshot);
}

crossinkimport::ApplyResult CrossPointSettings::importCrossInkSettings(
    crossinkimport::ImportKind kind, char (&snapshot)[crossinkimport::SNAPSHOT_PATH_SIZE]) {
  std::lock_guard<std::mutex> lock(storeMutex);
  snapshot[0] = '\0';
  if (crossInkImportWriteBlocked || crossInkImportGuardBlocked) return crossinkimport::ApplyResult::RecoveryRequired;
  HalFile file = Storage.openChecked(getFilePath(), O_RDONLY);
  if (!file) return crossinkimport::ApplyResult::Unavailable;
  const uint64_t size = file.fileSize64();
  if (file.isDirectory() || !size || size > 32768) {
    file.close();
    return crossinkimport::ApplyResult::Unavailable;
  }
  auto original = makeUniqueNoThrow<char[]>(static_cast<size_t>(size));
  if (!original) {
    LOG_ERR("CROSSINK", "Could not allocate %u bytes for current settings", static_cast<unsigned>(size));
    file.close();
    return crossinkimport::ApplyResult::Unavailable;
  }
  const bool read = file.read(original.get(), static_cast<size_t>(size)) == static_cast<int>(size);
  const bool closed = file.close();
  if (!read || !closed) return crossinkimport::ApplyResult::Unavailable;
  JsonDocument proposed;
  if (deserializeJson(proposed, static_cast<const char*>(original.get()), static_cast<size_t>(size),
                      DeserializationOption::NestingLimit(16)) ||
      !proposed.is<JsonObjectConst>())
    return crossinkimport::ApplyResult::Unavailable;
  for (const char* key : {"sdFontFamilyName", "dictionaryName", "keyboardLayouts", "bleRemotes"})
    proposed.as<JsonObject>().remove(key);
  toJson(proposed);
  if (proposed.overflowed()) return crossinkimport::ApplyResult::Unavailable;
  const auto bindings = readerquickactions::bindingValues(*this);
  const bool hasQuickTrigger = std::any_of(bindings.begin(), bindings.end(), [](uint8_t value) { return value != 0; });
  crossinkimport::ApplyResult result = crossinkimport::ApplyResult::Unavailable;
  switch (kind) {
    case crossinkimport::ImportKind::Controls:
      result = crossinkimport::publishControls(proposed, true, hasQuickTrigger, original.get(),
                                               static_cast<size_t>(size), snapshot);
      break;
    case crossinkimport::ImportKind::ReaderBars:
      result = crossinkimport::publishReaderBars(proposed, original.get(), static_cast<size_t>(size), snapshot);
      break;
    case crossinkimport::ImportKind::Library:
      result = crossinkimport::publishLibrary(proposed, original.get(), static_cast<size_t>(size), snapshot);
      break;
    case crossinkimport::ImportKind::Preferences:
      result = crossinkimport::publishPreferences(proposed, original.get(), static_cast<size_t>(size), snapshot);
      break;
  }
  if (result == crossinkimport::ApplyResult::WriteUncertain ||
      result == crossinkimport::ApplyResult::RecoveryRequired) {
    crossInkImportWriteBlocked = true;
    settingsSaveNotice().recordFailure();
  } else if (result == crossinkimport::ApplyResult::Published) {
    const bool pendingResave = resaveRequested;
    if (!fromJson(proposed.as<JsonVariantConst>())) {
      crossInkImportWriteBlocked = true;
      settingsSaveNotice().recordFailure();
      resaveRequested = pendingResave;
      return crossinkimport::ApplyResult::WriteUncertain;
    }
    resaveRequested = pendingResave;
  }
  return result;
}

crossinkimport::RecoveryResult CrossPointSettings::recoverUnchangedCrossInkImport() {
  std::lock_guard<std::mutex> lock(storeMutex);
  const auto result = crossinkimport::recoverUnchangedImport();
  if (result == crossinkimport::RecoveryResult::Recovered) {
    crossInkImportWriteBlocked = false;
    crossInkImportGuardBlocked = false;
    crossInkImportGuardChecked = true;
  }
  return result;
}

bool CrossPointSettings::loadSpecializedFromFile() {
  bool ok = false;
  bool doResave = false;
  {
    std::lock_guard<std::mutex> lock(storeMutex);
    if (!crossInkImportGuardChecked) {
      crossInkImportGuardBlocked = crossinkimport::inspectImports() != crossinkimport::GuardState::Clear;
      crossInkImportGuardChecked = true;
      if (crossInkImportGuardBlocked) settingsSaveNotice().recordFailure();
    }
    struct LoadContext {
      CrossPointSettings* settings;
      bool ok;
      bool attention;
    } context{this, false, false};
    const bool locked = Storage.withStorageLock(
        [](void* raw) {
          auto& c = *static_cast<LoadContext*>(raw);
          constexpr char backup[] = "/.crosspoint/settings.json.bak";
          constexpr char temporary[] = "/.crosspoint/settings.json.tmp";
          const auto state = settingsrecovery::probe(getFilePath(), backup, temporary);
          const auto choice = state.bootDecision();
          c.attention = !choice.allowSave;
          const char* path = choice.candidate == settingsrecovery::BootCandidate::Primary  ? getFilePath()
                             : choice.candidate == settingsrecovery::BootCandidate::Backup ? backup
                                                                                           : nullptr;
          if (!path) return true;
          JsonDocument doc;
          HalFile file = Storage.openChecked(path, O_RDONLY);
          if (!file) {
            c.attention = true;
            return true;
          }
          const uint64_t size = file.fileSize64();
          if (file.isDirectory() || size == 0 || size > 32768) {
            file.close();
            c.attention = true;
            return true;
          }
          auto bytes = makeUniqueNoThrow<char[]>(static_cast<size_t>(size) + 1);
          if (!bytes) {
            file.close();
            c.attention = true;
            return true;
          }
          const bool read = file.read(bytes.get(), static_cast<size_t>(size)) == static_cast<int>(size);
          const bool closed = file.close();
          bytes[size] = '\0';
          if (!read || !closed ||
              deserializeJson(doc, static_cast<const char*>(bytes.get()), static_cast<size_t>(size)) ||
              !doc.is<JsonObjectConst>()) {
            c.attention = true;
            return true;
          }
          c.settings->resaveRequested = false;
          c.ok = c.settings->fromJson(doc.as<JsonVariantConst>());
          return true;
        },
        &context);
    ok = locked && context.ok;
    if (!locked || context.attention) settingsSaveNotice().recordFailure();
    doResave = ok && resaveRequested;
    resaveRequested = false;
  }
  if (doResave && !saveToFile()) LOG_ERR("CPS", "Could not save migrated settings");
  return ok;
}

void CrossPointSettings::validateFrontButtonMapping(CrossPointSettings& settings) {
  const uint8_t mapping[] = {settings.frontButtonBack, settings.frontButtonConfirm, settings.frontButtonLeft,
                             settings.frontButtonRight};
  for (size_t i = 0; i < 4; i++) {
    for (size_t j = i + 1; j < 4; j++) {
      if (mapping[i] == mapping[j]) {
        settings.frontButtonBack = FRONT_HW_BACK;
        settings.frontButtonConfirm = FRONT_HW_CONFIRM;
        settings.frontButtonLeft = FRONT_HW_LEFT;
        settings.frontButtonRight = FRONT_HW_RIGHT;
        return;
      }
    }
  }
}

uint8_t CrossPointSettings::sleepTimeoutEnumToMinutes(const uint8_t legacyValue) {
  switch (legacyValue) {
    case SLEEP_1_MIN:
      return 1;
    case SLEEP_5_MIN:
      return 5;
    case SLEEP_15_MIN:
      return 15;
    case SLEEP_30_MIN:
      return 30;
    case SLEEP_10_MIN:
    default:
      return 10;
  }
}

void CrossPointSettings::toJson(JsonDocument& doc) const {
  const CrossPointSettings& s = *this;

  auto visit = [&](const SettingInfo& info) {
    if (!info.key) return;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) return;

    if (info.stringOffset) {
      const char* strPtr = (const char*)&s + info.stringOffset;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        doc[obfKey] = obfuscation::obfuscateToBase64(strPtr);
      } else {
        doc[info.key] = strPtr;
      }
    } else {
      doc[info.key] = s.*(info.valuePtr);
    }
  };
  forEachBaseSetting(visit);

  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  doc["frontButtonBack"] = frontButtonBack;
  doc["frontButtonConfirm"] = frontButtonConfirm;
  doc["frontButtonLeft"] = frontButtonLeft;
  doc["frontButtonRight"] = frontButtonRight;
  // Font family and size — both use dynamic getter/setters in SettingsList (the
  // option lists depend on the SD font registry), so the generic loop skips them.
  doc["fontFamily"] = STORED_NOTOSANS;
  doc["fontSize"] = fontPointSize;
  // SD card font family name — not in SettingsList, save manually
  if (sdFontFamilyName[0] != '\0') {
    doc["sdFontFamilyName"] = sdFontFamilyName;
  }
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, save manually
  if (dictionaryName[0] != '\0') {
    doc["dictionaryName"] = dictionaryName;
  }
  // The displayed option list varies by board, so this setting uses a dynamic
  // display-index mapping and is skipped by the generic persistence loop.
  doc["longPressMenuFunction"] = longPressMenuFunction;
  doc["shortPwrBtn"] = shortPwrBtn;

  // Language -- managed by LanguageSelectActivity, not in SettingsList.
  // Stored as ISO code string ("EN", "DE", ...) for stability across enum reorders.
  doc["language"] = (language < getLanguageCount()) ? LANGUAGE_CODES[language] : "EN";

  // A uint16_t mask, so it does not fit the uint8_t generic loop. Omitted while
  // unconfigured, so the default keeps following the UI language.
  if (keyboardLayouts != 0) {
    doc["keyboardLayouts"] = keyboardLayouts;
  }

  doc["readerBarsEnabled"] = readerBarsEnabled;
  auto bars = doc["readerBarsV1"].to<JsonObject>();
  writeReaderBar(bars["top"].to<JsonObject>(), readerTopBar);
  writeReaderBar(bars["bottom"].to<JsonObject>(), readerBottomBar);

  readergestures::writeBindings(doc, twoFingerActions);
  readerquickactions::write(doc, readerQuickActions);
  bleturner::writeJson(ble, doc);
  bookReaderSettings.writeGlobal(doc.as<JsonObject>());
}

bool CrossPointSettings::fromJson(JsonVariantConst doc) {
  bookReaderSettings.beforeGlobalLoad(*this);
  CrossPointSettings& s = *this;
  bool needsResave = false;
  const auto quickBindingsBefore = readerquickactions::bindingValues(s);

  auto clamp = [](uint8_t val, uint8_t maxVal, uint8_t def) -> uint8_t { return val < maxVal ? val : def; };

  auto visit = [&](const SettingInfo& info) {
    if (!info.key) return;
    // Dynamic entries (KOReader etc.) are stored in their own files — skip.
    if (!info.valuePtr && !info.stringOffset) return;

    if (info.stringOffset) {
      // destPtr starts out holding the struct-initializer default; it stays that
      // way unless the document actually carries a value for this key.
      char* destPtr = (char*)&s + info.stringOffset;
      if (info.stringMaxLen == 0) {
        LOG_ERR("CPS", "Misconfigured SettingInfo: stringMaxLen is 0 for key '%s'", info.key);
        destPtr[0] = '\0';
        needsResave = true;
        return;
      }

      bool loaded = false;
      if (info.obfuscated) {
        char obfKey[OBF_KEY_BUF];
        snprintf(obfKey, sizeof(obfKey), "%s_obf", info.key);
        bool ok = false;
        bool tooLong = false;
        const std::string decoded =
            obfuscation::deobfuscateFromBase64(doc[obfKey] | "", info.stringMaxLen - 1, &ok, &tooLong);
        if (tooLong) {
          LOG_ERR("CPS", "Oversized obfuscated value for key '%s'", info.key);
          needsResave = true;
        }
        if (ok && !decoded.empty()) {
          copyToField(destPtr, decoded.c_str(), info.stringMaxLen);
          loaded = true;
        }
      }
      if (!loaded) {
        // Read as const char*, never `| std::string(...)`: ArduinoJson's
        // std::string converter drags a per-TU copy of the serializer into
        // flash. See the note in PersistableStore.h.
        const char* raw = doc[info.key].is<const char*>() ? doc[info.key].as<const char*>() : nullptr;
        if (raw) {
          // Obfuscated field recovered from a legacy plaintext value -> resave.
          if (info.obfuscated && strcmp(raw, destPtr) != 0) needsResave = true;
          copyToField(destPtr, raw, info.stringMaxLen);
        }
      }
    } else {
      const uint8_t fieldDefault = s.*(info.valuePtr);  // struct-initializer default, read before we overwrite it
      uint8_t v = doc[info.key] | fieldDefault;
      if (info.type == SettingType::ENUM) {
        v = clamp(v, (uint8_t)info.enumLabels().size(), fieldDefault);
      } else if (info.type == SettingType::TOGGLE) {
        v = clamp(v, (uint8_t)2, fieldDefault);
      } else if (info.type == SettingType::VALUE) {
        if (v < info.valueRange.min)
          v = info.valueRange.min;
        else if (v > info.valueRange.max)
          v = info.valueRange.max;
      }
      s.*(info.valuePtr) = v;
    }
  };
  forEachBaseSetting(visit);
  readershortcuts::readEmbeddedLegacyPower(doc, {&powerHoldAction, &powerUpChordAction});
  readershortcuts::readEmbeddedLegacySideOrientation(doc, readerSideFollowOrientation);
  readershortcuts::readEmbeddedLegacySides(
      doc, {&readerUpTap, &readerUpHold, &readerDownTap, &readerDownHold, &readerSideChord});
  if (BoardConfig::hasTouch()) {
    // A normal settings save later persists imported values under modern keys.
    readergestures::readEmbeddedLegacyEdges(
        doc, {&readerLeftEdgeUp, &readerLeftEdgeDown, &readerRightEdgeUp, &readerRightEdgeDown});
  }
  needsResave |= readergestures::readBindings(doc, twoFingerActions);
  needsResave |= readerquickactions::read(doc, readerQuickActions);
  const bool importedLegacyQuickSlots = readershortcuts::readEmbeddedLegacyQuickSlots(doc, readerQuickActions);

  // Plugin System did not exist in older settings files. Preserve the behavior
  // of users who already have SD plugins installed, while keeping the new
  // default disabled on clean installs.
  if (doc["pluginsEnabled"].isNull()) {
    pluginsEnabled = PluginLocations::anyPluginInstalledOnDisk() ? 1 : 0;
    needsResave = true;
  }

  const auto indentSpaces = doc["paragraphIndentSpaces"];
  const bool hasSavedWidth = indentSpaces.is<int>();
  const int savedWidth = hasSavedWidth ? indentSpaces.as<int>() : 0;
  paragraphIndentSpaces = migrateParagraphIndentSpaces(hasSavedWidth, savedWidth, extraParagraphSpacing != 0);
  if (!hasSavedWidth || savedWidth < 0 || savedWidth > 5) needsResave = true;

  // Older files stored one combined touch mode under "touchReaderControls":
  // 0=off, 1=tap, 2=swipe, 3=inverted tap. Split it into the master toggle
  // plus the per-direction gesture pair (the generic loop above already folded
  // out-of-range toggle values back to the On default).
  if (doc["pageTurnGesture"].isNull() && doc["previousPageGesture"].isNull() &&
      doc["touchReaderControls"].is<uint8_t>()) {
    const uint8_t mode = doc["touchReaderControls"].as<uint8_t>();
    if (mode >= 1 && mode <= 3) {
      touchReaderControls = TOUCH_READER_ON;
      pageTurnGesture = mode == 1 ? TAP_ONLY : mode == 2 ? SWIPE_ONLY : INVERTED_TAP;
      previousPageGesture = pageTurnGesture;
      needsResave = true;
    }
  }

  if (doc["sleepTimeoutMinutes"].isNull() && !doc["sleepTimeout"].isNull()) {
    const uint8_t legacyValue =
        clamp(doc["sleepTimeout"] | (uint8_t)SLEEP_10_MIN, SLEEP_TIMEOUT_COUNT, (uint8_t)SLEEP_10_MIN);
    sleepTimeoutMinutes = sleepTimeoutEnumToMinutes(legacyValue);
    needsResave = true;
  }
  // "lineSpacing" saves predate the 1.0 option: Tight/Normal/Wide/Extra Wide (0..3) become 1.1..1.45.
  if (doc["lineHeight"].isNull() && doc["lineSpacing"].is<uint8_t>()) {
    const int legacyValue = doc["lineSpacing"].as<uint8_t>();
    lineSpacing = legacyValue + 1 < LINE_COMPRESSION_COUNT ? static_cast<uint8_t>(legacyValue + 1) : LINE_SPACING_120;
    needsResave = true;
  }
  // Front button remap — managed by RemapFrontButtons sub-activity, not in SettingsList.
  frontButtonBack = clamp(doc["frontButtonBack"] | (uint8_t)FRONT_HW_BACK, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_BACK);
  frontButtonConfirm =
      clamp(doc["frontButtonConfirm"] | (uint8_t)FRONT_HW_CONFIRM, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_CONFIRM);
  frontButtonLeft = clamp(doc["frontButtonLeft"] | (uint8_t)FRONT_HW_LEFT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_LEFT);
  frontButtonRight =
      clamp(doc["frontButtonRight"] | (uint8_t)FRONT_HW_RIGHT, FRONT_BUTTON_HARDWARE_COUNT, FRONT_HW_RIGHT);
  validateFrontButtonMapping(s);

  // Reader font size — an actual point size since 1.5. Files written by 1.4 and
  // earlier hold the old SMALL/MEDIUM/LARGE/EXTRA_LARGE slot in 0..3; no font is
  // renderable at those sizes, so the range is unambiguous and folds to the
  // point sizes those slots used to mean. Drop this once 1.4 upgrades are done.
  uint8_t storedFontSize = doc["fontSize"] | DEFAULT_FONT_POINT_SIZE;
  if (storedFontSize <= LEGACY_FONT_SIZE_MAX) {
    storedFontSize = 12 + storedFontSize * 2;  // 0,1,2,3 -> 12,14,16,18
    needsResave = true;
  }
  fontPointSize = storedFontSize;

  // Font family — uses dynamic getter/setter in SettingsList so the generic loop skips it.
  const uint8_t storedFontFamily = doc["fontFamily"] | STORED_NOTOSANS;
  fontFamily = NOTOSANS;
  // These controls use board-specific display-index mappings and are skipped by
  // the generic settings loop. Persisted action IDs remain stable across boards.
  longPressMenuFunction = clamp(doc["longPressMenuFunction"] | (uint8_t)LP_MENU_DISABLED,
                                (uint8_t)LONG_PRESS_MENU_FUNCTION_COUNT, (uint8_t)LP_MENU_DISABLED);
  shortPwrBtn = clamp(doc["shortPwrBtn"] | (uint8_t)IGNORE, (uint8_t)SHORT_PWRBTN_COUNT, (uint8_t)IGNORE);
  // Do not force a resave solely for this import: a normal settings save will
  // persist the modern binding, while loading leaves the legacy file intact.
  readershortcuts::migrateLegacyHomePowerTap(doc, BoardConfig::hasHomeKey(), powerTapAction);
  if (BoardConfig::hasHomeKey() && doc["homeButtonLongPressAction"].isNull() &&
      !doc["longPressMenuFunction"].isNull()) {
    static constexpr HomeButtonAction LEGACY[] = {HomeButtonAction::Sync,       HomeButtonAction::Ignore,
                                                  HomeButtonAction::Bookmark,   HomeButtonAction::Dictionary,
                                                  HomeButtonAction::ReaderMenu, HomeButtonAction::CreateClipping};
    if (s.longPressMenuFunction < sizeof(LEGACY) / sizeof(LEGACY[0])) {
      s.homeButtonLongPressAction = static_cast<uint8_t>(LEGACY[s.longPressMenuFunction]);
      needsResave = true;
    }
  }

  // SD card font family name — not in SettingsList, load manually
  const char* sfn = doc["sdFontFamilyName"] | "";
  strncpy(sdFontFamilyName, sfn, sizeof(sdFontFamilyName) - 1);
  sdFontFamilyName[sizeof(sdFontFamilyName) - 1] = '\0';
  if (sdFontFamilyName[0] == '\0') {
    switch (storedFontFamily) {
      case LEGACY_NOTOSERIF:
        copyToField(sdFontFamilyName, "NotoSerif", sizeof(sdFontFamilyName));
        break;
      case LEGACY_OPENDYSLEXIC:
        copyToField(sdFontFamilyName, "OpenDyslexic", sizeof(sdFontFamilyName));
        break;
    }
  }
  if (storedFontFamily != STORED_NOTOSANS) needsResave = true;
  // Dictionary folder name — uses dynamic getter/setter in SettingsList, load manually
  copyToField(dictionaryName, doc["dictionaryName"] | "", sizeof(dictionaryName));
  // Language -- stored as code string for stability across enum reorders.
  if (doc["language"].is<const char*>()) {
    language = static_cast<uint8_t>(I18n::languageFromCode(doc["language"].as<const char*>()));
  }

  // Absent means unconfigured, which is the default.
  if (doc["keyboardLayouts"].is<uint16_t>()) {
    keyboardLayouts = doc["keyboardLayouts"].as<uint16_t>();
  }

  // An absent/invalid custom layout starts from the existing CrossPoint footer.
  readerTopBar = {};
  readerBottomBar = {};
  if (statusBarBattery) readerBottomBar.slots[0] = ReaderBarItem::Battery;
  if (statusBarClock != STATUS_BAR_CLOCK_HIDE) {
    readerBottomBar.slots[statusBarClock == STATUS_BAR_CLOCK_LEFT ? 1 : 6] = ReaderBarItem::Clock;
  }
  if (statusBarChapterPageCount) readerBottomBar.slots[4] = ReaderBarItem::ChapterPages;
  if (statusBarBookProgressPercentage) readerBottomBar.slots[5] = ReaderBarItem::BookPercent;
  if (statusBarTitle == BOOK_TITLE) readerBottomBar.slots[3] = ReaderBarItem::BookTitle;
  if (statusBarTitle == CHAPTER_TITLE) readerBottomBar.slots[3] = ReaderBarItem::ChapterTitle;
  readerBottomBar.progress = statusBarProgressBar;
  readerBottomBar.thickness = statusBarProgressBarThickness;
  const auto bars = doc["readerBarsV1"];
  if (!bars.isNull()) {
    const bool topValid = readReaderBar(bars["top"], readerTopBar);
    const bool bottomValid = readReaderBar(bars["bottom"], readerBottomBar);
    needsResave = needsResave || !topValid || !bottomValid;
    readerBarsEnabled = topValid && bottomValid && (doc["readerBarsEnabled"] | false);
  } else {
    readerBarsEnabled = false;
  }

  if (!bleturner::readJson(ble, doc)) {
    needsResave = true;
  }

  const auto quickBindingsAfterLoad = readerquickactions::bindingValues(s);
  const bool currentQuickTrigger = std::any_of(quickBindingsAfterLoad.begin(), quickBindingsAfterLoad.end(),
                                               [](uint8_t value) { return value != 0; });
  const bool invalidLegacyQuickSlots =
      doc["readerQuickActionsV1"].isUnbound() && !doc["quickActionSlots"].isUnbound() && !importedLegacyQuickSlots;
  readershortcuts::readEmbeddedLegacySideQuickAction(
      doc, {&readerUpTap, &readerUpHold, &readerDownTap, &readerDownHold, &readerSideChord},
      currentQuickTrigger || invalidLegacyQuickSlots);
  readershortcuts::readEmbeddedLegacyPowerQuickAction(doc, {&powerTapAction, &powerHoldAction, &powerUpChordAction},
                                                      currentQuickTrigger || invalidLegacyQuickSlots);
  needsResave |= readerquickactions::normalizeChangedBindings(s, quickBindingsBefore);

  if (needsResave) {
    LOG_DBG("CPS", "Resaving settings to update format");
    requestResave();
  }

  bookReaderSettings.afterGlobalLoad(*this);
  uiscale::publish(uiScale);
  LOG_DBG("CPS", "Settings loaded from file");

  return true;
}

CrossPointSettings::StatusBarSpec CrossPointSettings::statusBarSpec() const {
  StatusBarSpec spec;
  spec.showChapterPageCount = statusBarChapterPageCount != 0;
  spec.showBookProgressPercent = statusBarBookProgressPercentage != 0;
  spec.titleMode = statusBarTitle;
  spec.showBattery = statusBarBattery != 0;
  spec.showBatteryPercent = hideBatteryPercentage == HIDE_NEVER;
  spec.clockMode = statusBarClock;
  spec.clock12h = clockFormat == 1;
  spec.progressBarMode = statusBarProgressBar;
  spec.progressBarHeightPx =
      statusBarProgressBar != HIDE_PROGRESS ? static_cast<uint8_t>((statusBarProgressBarThickness + 1) * 2) : 0;
  spec.xtcMode = xtcStatusBarMode;
  return spec;
}

ReaderRenderSpec CrossPointSettings::readerRenderSpec(const uint16_t viewportWidth,
                                                      const uint16_t viewportHeight) const {
  ReaderRenderSpec spec;
  spec.fontId = getReaderFontId();
  spec.lineCompression = getReaderLineCompression();
  spec.characterSpacing = getCharacterSpacing();
  spec.wordSpacingPercent = wordSpacing;
  spec.extraParagraphSpacing = extraParagraphSpacing != 0;
  spec.paragraphIndentSpaces = paragraphIndentSpaces;
  spec.paragraphAlignment = paragraphAlignment;
  spec.viewportWidth = viewportWidth;
  spec.viewportHeight = viewportHeight;
  spec.hyphenationEnabled = hyphenationEnabled != 0;
  spec.embeddedStyle = embeddedStyle != 0;
  // Same layout as Display, so switching between them keeps the section cache.
  spec.imageRendering = imageRendering == IMAGES_DISPLAY_BW ? static_cast<uint8_t>(IMAGES_DISPLAY) : imageRendering;
  spec.focusReadingEnabled = focusReadingEnabled != 0;
  spec.renderMode = normalizeEpubRenderMode(epubRenderMode);
  return spec;
}

float CrossPointSettings::getReaderLineCompression() const {
  return LINE_SPACING_MULTIPLIERS[lineSpacing < LINE_COMPRESSION_COUNT ? lineSpacing : LINE_SPACING_120];
}

unsigned long CrossPointSettings::getSleepTimeoutMs() const {
  if (sleepTimeoutMinutes >= SLEEP_TIMEOUT_NEVER_MINUTES) return 0UL;
  const uint8_t minutes =
      std::clamp(sleepTimeoutMinutes, MIN_SLEEP_TIMEOUT_MINUTES, static_cast<uint8_t>(SLEEP_TIMEOUT_NEVER_MINUTES - 1));
  return static_cast<unsigned long>(minutes) * 60UL * 1000UL;
}

int CrossPointSettings::getRefreshFrequency() const {
  switch (refreshFrequency) {
    case REFRESH_1:
      return 1;
    case REFRESH_5:
      return 5;
    case REFRESH_10:
      return 10;
    case REFRESH_15:
    default:
      return 15;
    case REFRESH_30:
      return 30;
    case REFRESH_NEVER:
      // Effectively disables the periodic full refresh; the page counter
      // counts down from here and never reaches the threshold in practice.
      return std::numeric_limits<int>::max();
  }
}

void CrossPointSettings::clearSdFontFamily() {
  sdFontFamilyName[0] = '\0';
  fontPointSize =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  saveToFile();
}

int CrossPointSettings::getReaderFontId() const {
  // Check SD card font first
  if (sdFontFamilyName[0] != '\0' && sdFontIdResolver) {
    int id = sdFontIdResolver(sdFontResolverCtx, sdFontFamilyName, fontPointSize);
    if (id != 0) return id;
    // Fall through to built-in if SD font not found
  }

  // A built-in family only exists at BUILTIN_READER_POINT_SIZES, so a size
  // carried over from an SD family may not be one of them. ensureLoaded()
  // normally persists the snap; snap again here (without allocating — this runs
  // in the page render loop) so rendering is correct even before it has run.
  const uint8_t pt =
      snapToNearestPointSize(BUILTIN_READER_POINT_SIZES, std::size(BUILTIN_READER_POINT_SIZES), fontPointSize);
  switch (pt) {
    case 12:
      return NOTOSANS_12_FONT_ID;
    case 16:
      return NOTOSANS_16_FONT_ID;
    case 18:
      return NOTOSANS_18_FONT_ID;
    case 14:
    default:
      return NOTOSANS_14_FONT_ID;
  }
}

void CrossPointSettings::setTwoFingerAction(size_t index, uint8_t value) {
  if (index >= twoFingerActions.size()) return;
  twoFingerActions[index] = value;
  readergestures::normalize(twoFingerActions, static_cast<int>(index));
}

void CrossPointSettings::beginBookReaderSettings(const BookReaderSettings& book) {
  std::lock_guard<std::mutex> lock(storeMutex);
  bookReaderSettings.begin(*this, book);
}

bool CrossPointSettings::bookReaderSettingsActive() {
  std::lock_guard<std::mutex> lock(storeMutex);
  return bookReaderSettings.active();
}

BookReaderSettings CrossPointSettings::captureBookReaderSettings() {
  std::lock_guard<std::mutex> lock(storeMutex);
  return bookReaderSettings.capture(*this);
}

void CrossPointSettings::resetBookReaderSettings() {
  std::lock_guard<std::mutex> lock(storeMutex);
  bookReaderSettings.reset(*this);
}

void CrossPointSettings::endBookReaderSettings() {
  std::lock_guard<std::mutex> lock(storeMutex);
  bookReaderSettings.end(*this);
}
