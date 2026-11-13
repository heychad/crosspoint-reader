#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;
  bool saveForCompletionLocked(bool finishPublishedOnly = false) const;
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  bool saveSleepPreferencesLocked() const;
#endif

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_AVOID_REPEAT_LOOKBACK = 5;

  std::string openEpubPath;
  std::string favoriteSleepImagePath;
  std::string preferredSleepFolderPath;
  uint32_t sleepLcgCount = 0;
  uint32_t sleepLcgState = 0;
  uint32_t sleepOverlayLcgCount = 0;
  uint32_t sleepOverlayLcgState = 0;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  bool saveSpecializedToFile() const;
  bool loadSpecializedFromFile();
  bool updateSleepFavoriteForPageOverlay(const std::string& path);
  bool updateSleepFolderForPageOverlay(const std::string& path);
  bool recoverSleepStateForPageOverlay();
#endif
  // A move changes the reopen path only when it still refers to that book.
  // Failed writes restore the in-memory path so the operation can retry.
  bool updateOpenEpubPathPersisted(const std::string& oldPath, const std::string& newPath);

  void getSleepLCG(uint32_t& seed, uint32_t& state, uint16_t& indexesLeft, uint16_t& indexesTotal) const;
  void getSleepOverlayLCG(uint32_t& seed, uint32_t& state, uint16_t& indexesLeft, uint16_t& indexesTotal) const;

  void setSleepLCG(uint32_t seed, uint32_t state, uint16_t indexesLeft, uint16_t indexesTotal);
  void setSleepOverlayLCG(uint32_t seed, uint32_t state, uint16_t indexesLeft, uint16_t indexesTotal);
};

#define APP_STATE CrossPointState::getInstance()
