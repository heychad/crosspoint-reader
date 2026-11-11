#include "CrossPointState.h"

#include <HalStorage.h>
#include <MoveReferenceSafeWrite.h>

#include <utility>
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
#include "activities/reader/StatsSaveNotice.h"
#endif

#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
bool CrossPointState::saveSpecializedToFile() const {
  std::lock_guard<std::mutex> lock(storeMutex);
  JsonDocument document;
  toJson(document);
  struct Context {
    const JsonDocument* document;
  } context{&document};
  const bool saved = Storage.withStorageLock(
      [](void* raw) {
        const auto& ctx = *static_cast<Context*>(raw);
        if (Storage.lookupPath("/.crosspoint/state.json.tmp") != HalStorage::PathStatus::Missing ||
            Storage.lookupPath("/.crosspoint/state.json.bak") != HalStorage::PathStatus::Missing ||
            Storage.lookupPath("/.crosspoint/state.json.sleep-tmp") != HalStorage::PathStatus::Missing ||
            Storage.lookupPath("/.crosspoint/state.json.sleep-bak") != HalStorage::PathStatus::Missing)
          return false;
        return PersistableStoreBase::writeDocToFile(getFilePath(), *ctx.document);
      },
      &context);
  if (!saved) stateSaveNotice().recordFailure();
  return saved;
}

bool CrossPointState::loadSpecializedFromFile() {
  std::lock_guard<std::mutex> lock(storeMutex);
  JsonDocument document;
  struct Context {
    JsonDocument* document;
    bool attention;
  } context{&document, false};
  const bool loaded = Storage.withStorageLock(
      [](void* raw) {
        auto& ctx = *static_cast<Context*>(raw);
        auto& doc = *ctx.document;
        const auto moveTemp = Storage.lookupPath("/.crosspoint/state.json.tmp");
        const auto moveBackup = Storage.lookupPath("/.crosspoint/state.json.bak");
        const auto sleepTemp = Storage.lookupPath("/.crosspoint/state.json.sleep-tmp");
        const auto sleepBackup = Storage.lookupPath("/.crosspoint/state.json.sleep-bak");
        ctx.attention = moveTemp != HalStorage::PathStatus::Missing || moveBackup != HalStorage::PathStatus::Missing ||
                        sleepTemp != HalStorage::PathStatus::Missing || sleepBackup != HalStorage::PathStatus::Missing;
        if (PersistableStoreBase::readDocFromFile(getFilePath(), doc, 65536) && doc.is<JsonObject>()) return true;
        ctx.attention = ctx.attention || Storage.lookupPath(getFilePath()) != HalStorage::PathStatus::Missing;
        doc.clear();
        const char* backupPath = nullptr;
        if (moveBackup == HalStorage::PathStatus::Present && sleepBackup == HalStorage::PathStatus::Missing)
          backupPath = "/.crosspoint/state.json.bak";
        if (sleepBackup == HalStorage::PathStatus::Present && moveBackup == HalStorage::PathStatus::Missing)
          backupPath = "/.crosspoint/state.json.sleep-bak";
        const bool backup =
            backupPath && PersistableStoreBase::readDocFromFile(backupPath, doc, 65536) && doc.is<JsonObject>();
        ctx.attention = ctx.attention || backup;
        return backup;
      },
      &context);
  if (context.attention) stateSaveNotice().recordFailure();
  return loaded && fromJson(document.as<JsonVariantConst>());
}

bool CrossPointState::updateSleepFavoriteForPageOverlay(const std::string& path) {
  std::lock_guard<std::mutex> lock(storeMutex);
  std::string previous = favoriteSleepImagePath;
  favoriteSleepImagePath = path;
  if (saveSleepPreferencesLocked()) return true;
  favoriteSleepImagePath = std::move(previous);
  stateSaveNotice().recordFailure();
  return false;
}

bool CrossPointState::updateSleepFolderForPageOverlay(const std::string& path) {
  std::lock_guard<std::mutex> lock(storeMutex);
  std::string previous = preferredSleepFolderPath;
  preferredSleepFolderPath = path;
  if (saveSleepPreferencesLocked()) return true;
  preferredSleepFolderPath = std::move(previous);
  stateSaveNotice().recordFailure();
  return false;
}

bool CrossPointState::recoverSleepStateForPageOverlay() {
  std::lock_guard<std::mutex> lock(storeMutex);
  const bool recovered = movereference::recoverSleepPreferences([](const char* path) {
    JsonDocument doc;
    return PersistableStoreBase::readDocFromFile(path, doc, 65536) && doc.is<JsonObject>();
  });
  if (!recovered) stateSaveNotice().recordFailure();
  return recovered;
}

bool CrossPointState::saveSleepPreferencesLocked() const {
  for (const char* path : {"/.crosspoint/state.json.tmp", "/.crosspoint/state.json.bak",
                           "/.crosspoint/state.json.sleep-tmp", "/.crosspoint/state.json.sleep-bak"})
    if (Storage.lookupPath(path) != HalStorage::PathStatus::Missing) return false;
  const auto status = Storage.lookupPath(getFilePath());
  if (status == HalStorage::PathStatus::Failed) return false;
  if (status == HalStorage::PathStatus::Present) {
    JsonDocument previous;
    if (!PersistableStoreBase::readDocFromFile(getFilePath(), previous, 65536) || !previous.is<JsonObject>())
      return false;
  }
  JsonDocument document;
  toJson(document);
  return PersistableStoreBase::writeStateSleepPreferences(document);
}
#endif

bool CrossPointState::saveForCompletionLocked() const {
  const std::string path = getFilePath();
  for (const char* suffix : {".tmp", ".bak"})
    if (Storage.lookupPath((path + suffix).c_str()) != HalStorage::PathStatus::Missing) return false;
  const auto status = Storage.lookupPath(path.c_str());
  if (status == HalStorage::PathStatus::Failed) return false;
  if (status == HalStorage::PathStatus::Present) {
    JsonDocument previous;
    if (!PersistableStoreBase::readDocFromFile(path.c_str(), previous, 65536) || !previous.is<JsonObject>())
      return false;
  }
  JsonDocument document;
  toJson(document);
  return PersistableStoreBase::writeDocToFileForMove(path.c_str(), document);
}

bool CrossPointState::updateOpenEpubPathPersisted(const std::string& oldPath, const std::string& newPath) {
  // Completion may already hold storage while another save holds storeMutex.
  std::unique_lock<std::mutex> lock(storeMutex, std::try_to_lock);
  if (!lock.owns_lock()) return false;
  if (openEpubPath != oldPath || oldPath == newPath) return true;
  const std::string target = newPath;
  auto saved = std::move(openEpubPath);
  openEpubPath = target;
  if (saveForCompletionLocked()) return true;
  openEpubPath = std::move(saved);
  return false;
}

namespace {

void getLCG(uint32_t packedState, uint32_t packedCount, uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
            uint16_t& indexesTotal) {
  indexesLeft = packedCount >> 16;
  indexesTotal = packedCount & 0xffff;
  seed = packedState >> 16;
  state = packedState & 0xffff;
}

void setLCG(uint32_t& packedState, uint32_t& packedCount, uint32_t seedVal, uint32_t stateVal, uint16_t indexesLeftVal,
            uint16_t indexesTotalVal) {
  packedCount = (static_cast<uint32_t>(indexesLeftVal) << 16) | indexesTotalVal;
  packedState = (seedVal << 16) | (stateVal & 0xffff);
}

}  // namespace

void CrossPointState::getSleepLCG(uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
                                  uint16_t& indexesTotal) const {
  getLCG(sleepLcgState, sleepLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::getSleepOverlayLCG(uint32_t& seed, uint32_t& state, uint16_t& indexesLeft,
                                         uint16_t& indexesTotal) const {
  getLCG(sleepOverlayLcgState, sleepOverlayLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::setSleepLCG(uint32_t seed, uint32_t state, uint16_t indexesLeft, uint16_t indexesTotal) {
  setLCG(sleepLcgState, sleepLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::setSleepOverlayLCG(uint32_t seed, uint32_t state, uint16_t indexesLeft, uint16_t indexesTotal) {
  setLCG(sleepOverlayLcgState, sleepOverlayLcgCount, seed, state, indexesLeft, indexesTotal);
}

void CrossPointState::toJson(JsonDocument& doc) const {
  doc["openEpubPath"] = openEpubPath;
  doc["favoriteSleepImagePath"] = favoriteSleepImagePath;
  doc["preferredSleepFolderPath"] = preferredSleepFolderPath;
  doc["sleepLcgCount"] = sleepLcgCount;
  doc["sleepLcgState"] = sleepLcgState;
  doc["sleepOverlayLcgCount"] = sleepOverlayLcgCount;
  doc["sleepOverlayLcgState"] = sleepOverlayLcgState;
  doc["readerActivityLoadCount"] = readerActivityLoadCount;
  doc["lastSleepFromReader"] = lastSleepFromReader;
  doc["showBootScreen"] = showBootScreen;
}

bool CrossPointState::fromJson(JsonVariantConst doc) {
  openEpubPath = doc["openEpubPath"] | "";
  favoriteSleepImagePath = doc["favoriteSleepImagePath"] | "";
  preferredSleepFolderPath = doc["preferredSleepFolderPath"] | "";

  sleepLcgCount = doc["sleepLcgCount"] | static_cast<uint32_t>(0);
  sleepLcgState = doc["sleepLcgState"] | static_cast<uint32_t>(0);

  sleepOverlayLcgCount = doc["sleepOverlayLcgCount"] | static_cast<uint32_t>(0);
  sleepOverlayLcgState = doc["sleepOverlayLcgState"] | static_cast<uint32_t>(0);

  readerActivityLoadCount = doc["readerActivityLoadCount"] | static_cast<uint8_t>(0);
  lastSleepFromReader = doc["lastSleepFromReader"] | false;
  showBootScreen = doc["showBootScreen"] | true;
  return true;
}
