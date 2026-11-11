#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <Memory.h>
#include <SDCardManager.h>
#include <esp_heap_caps.h>

#include "CheckedStoragePath.h"
#include "SmallFileIdentity.h"
#if FREEINK_CAP_USB_MSC
#include <UsbMassStorage.h>
#endif

#include <cassert>
#include <cstdio>
#include <cstring>
#include <utility>

#define SDCard SDCardManager::getInstance()

#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
extern "C" void crosspoint_sdmmc_monitor_arm();
extern "C" bool crosspoint_sdmmc_monitor_healthy();
#endif

namespace {
#if FREEINK_CAP_USB_MSC
freeink::UsbMassStorage usbMassStorage;
#endif
}  // namespace

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() {
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  crosspoint_sdmmc_monitor_arm();
#endif
  return SDCard.begin();
}

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
};

bool HalStorage::observedSdmmcHealthy() const {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  return SDCard.ready() && crosspoint_sdmmc_monitor_healthy();
#else
  return false;
#endif
}

bool HalStorage::beginMutationSnapshot(MutationSnapshot& snapshot) {
  StorageLock lock;
  if (!observedSdmmcHealthy()) mutationFence.poison();
  return mutationFence.beginSnapshot(snapshot);
}

bool HalStorage::mutationSnapshotUnchanged(const MutationSnapshot& snapshot) {
  StorageLock lock;
  if (!observedSdmmcHealthy()) mutationFence.poison();
  return mutationFence.unchanged(snapshot);
}

bool HalStorage::captureMutationMarker(MutationMarker& marker) {
  StorageLock lock;
  if (!observedSdmmcHealthy()) mutationFence.poison();
  return mutationFence.captureMarker(marker);
}

bool HalStorage::mutationMarkerUnchanged(const MutationMarker& marker) {
  StorageLock lock;
  if (!observedSdmmcHealthy()) mutationFence.poison();
  return mutationFence.markerUnchanged(marker);
}

HalStorage::SnapshotFormat HalStorage::snapshotFormat(const MutationSnapshot& snapshot) {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!observedSdmmcHealthy()) mutationFence.poison();
  if (!mutationFence.unchanged(snapshot)) return SnapshotFormat::Unknown;
  const uint8_t type = SDCard.filesystemType();
  if (type == FAT_TYPE_FAT16 || type == FAT_TYPE_FAT32) return SnapshotFormat::Fat;
  if (type == FAT_TYPE_EXFAT) return SnapshotFormat::ExFat;
#else
  (void)snapshot;
  mutationFence.poison();
#endif
  return SnapshotFormat::Unknown;
}

void HalStorage::prepareForDeepSleep() {
  StorageLock lock;
  mutationFence.poison();
  SDCard.shutdown();
}

bool HalStorage::beginUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  mutationFence.poison();  // USB can modify storage without passing through this HAL.
  auto* const blockDevice = SDCard.detachFilesystemForRawAccess();
  if (!blockDevice) {
    LOG_ERR("USB", "USB Drive requires a mounted SD filesystem");
    return false;
  }

  if (!usbMassStorage.begin(blockDevice)) {
    LOG_ERR("USB", "USB Drive MSC initialization failed");
    if (!SDCard.begin()) {
      LOG_ERR("USB", "Unable to remount SD card after USB Drive startup failure");
    }
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool HalStorage::disconnectUsbDriveHost() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  return usbMassStorage.disconnectHost();
#else
  return false;
#endif
}

bool HalStorage::usbDriveHostSuspended() const {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  return usbMassStorage.hostSuspended();
#else
  return false;
#endif
}

void HalStorage::endUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  usbMassStorage.end();
#endif
}

UsbDriveState HalStorage::usbDriveState() const {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  switch (usbMassStorage.state()) {
    case freeink::UsbMassStorageState::WaitingForHost:
      return UsbDriveState::WaitingForHost;
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      return UsbDriveState::Connected;
    case freeink::UsbMassStorageState::Ejected:
      return UsbDriveState::Ejected;
    case freeink::UsbMassStorageState::Disconnected:
      return UsbDriveState::Disconnected;
    case freeink::UsbMassStorageState::IoError:
      return UsbDriveState::IoError;
    case freeink::UsbMassStorageState::Idle:
      break;
  }
#endif
  return UsbDriveState::Unsupported;
}

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

#define HAL_STORAGE_MUTATION_CALL(method, ...) \
  HalStorage::StorageLock lock;                \
  mutationFence.mutationAttempt();             \
  const bool ok = SDCard.method(__VA_ARGS__);  \
  if (!ok) mutationFence.poison();             \
  return ok;

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

// Composed of already-locked HalStorage/HalFile operations; needs no
// StorageLock of its own.
bool HalStorage::readFileToString(const char* moduleName, const std::string& path, size_t cap, std::string& out) {
  out.clear();
  HalFile file;
  if (!openFileForRead(moduleName, path, file)) return false;
  if (file.isDirectory()) return false;
  const size_t size = file.fileSize();
  if (size == 0 || size > cap) return false;
  // string growth is a bare allocation under -fno-exceptions; probe first so
  // a large file on a fragmented heap fails soft instead of abort()ing.
  if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < size + 512) {
    LOG_ERR(moduleName, "readFileToString OOM: %u bytes for %s", static_cast<unsigned>(size), path.c_str());
    return false;
  }
  out.resize(size);
  return file.read(out.data(), size) == static_cast<int>(size);
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_MUTATION_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_MUTATION_CALL(ensureDirectoryExists, path); }

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) { registerWritable(); }
  void registerWritable() {
    if (file.isOpen() && file.isWritable())
      hasWriterLease = HalStorage::getInstance().mutationFence.writerOpened(writerLease);
  }
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518 and the HAL note in CLAUDE.md.
  ~Impl() {
    HalStorage::StorageLock lock;
    closeUnderLock();
  }
  bool closeUnderLock() {
    auto& fence = HalStorage::getInstance().mutationFence;
    if (hasWriterLease) fence.mutationAttempt();
    const bool priorWriteError = hasWriterLease && file.getWriteError();
    const bool closed = file.close();
    if (hasWriterLease) {
      if (!closed || priorWriteError)
        fence.poison();
      else
        fence.writerClosed(writerLease);
      hasWriterLease = false;
    }
    return closed;
  }
  FsFile file;
  checkedscan::StorageMutationFence::WriterLease writerLease;
  bool hasWriterLease = false;
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalStorage::SnapshotRead HalStorage::readDirectoryRecord(HalFile& directory, const MutationSnapshot& snapshot,
                                                         uint8_t* record) {
  StorageLock lock;
  if (!observedSdmmcHealthy()) mutationFence.poison();
  if (!mutationFence.unchanged(snapshot)) return SnapshotRead::Changed;
  if (!record || !directory.impl || !directory.impl->file.isDirectory()) return SnapshotRead::Failed;
  const int count = directory.impl->file.read(record, 32);
  if (!observedSdmmcHealthy()) mutationFence.poison();
  if (!mutationFence.unchanged(snapshot)) return SnapshotRead::Changed;
  if (count == 0 && directory.impl->file.getError() == 0) return SnapshotRead::End;
  if (count != 32 || directory.impl->file.getError() != 0) {
    mutationFence.poison();
    return SnapshotRead::Failed;
  }
  return SnapshotRead::Record;
}

HalStorage::SnapshotRead HalStorage::readDirectoryRecordAtPath(const char* path, const MutationSnapshot& snapshot,
                                                               DirectoryCursor& cursor, uint8_t* record) {
  StorageLock lock;
  if (!path || !record || cursor.failed) return SnapshotRead::Failed;
  const size_t length = strnlen(path, sizeof(cursor.path));
  if (!length || length >= sizeof(cursor.path) || path[0] != '/' ||
      (cursor.started && std::strcmp(path, cursor.path) != 0)) {
    cursor.failed = true;
    return SnapshotRead::Failed;
  }
  if (!mutationSnapshotUnchanged(snapshot) || (cursor.started && !mutationSnapshotUnchanged(cursor.snapshot))) {
    cursor.failed = true;
    return SnapshotRead::Changed;
  }
  if (cursor.nextRecord < cursor.buffered) {
    std::memcpy(record, cursor.records[cursor.nextRecord++], sizeof(cursor.records[0]));
    return SnapshotRead::Record;
  }
  if (cursor.ended) return SnapshotRead::End;
  if (!cursor.started && lookupPath(path) != PathStatus::Present) {
    cursor.failed = true;
    return SnapshotRead::Failed;
  }
  auto directory = SDCard.open(path, O_RDONLY);
  if (!directory || !directory.isDirectory()) {
    if (directory) directory.close();
    cursor.failed = true;
    return SnapshotRead::Failed;
  }
  if (cursor.started) {
    const fspos_t pos{cursor.position, cursor.cluster};
    directory.fsetpos(&pos);
  }
  uint8_t bytes[sizeof(cursor.records)];
  size_t buffered = 0;
  bool ended = false;
  bool readError = false;
  while (buffered < 8) {
    const int count = directory.read(bytes + buffered * 32, 32);
    if (directory.getError() != 0 || (count != 0 && count != 32)) {
      readError = true;
      break;
    }
    if (count == 0) {
      ended = true;
      break;
    }
    ++buffered;
    // Both FAT and exFAT use a zero first byte as the end-of-directory
    // marker. Leave interpretation to the decoder, but do not read past it.
    if (bytes[(buffered - 1) * 32] == 0) break;
  }
  fspos_t next{};
  if (!readError && buffered) directory.fgetpos(&next);
  const bool closed = directory.close();
  if (!closed || readError) mutationFence.poison();
  if (!closed || readError || !mutationSnapshotUnchanged(snapshot)) {
    cursor.failed = true;
    return SnapshotRead::Failed;
  }
  if (!cursor.started) {
    std::memcpy(cursor.path, path, length + 1);
    cursor.snapshot = snapshot;
    cursor.started = true;
  }
  cursor.ended = ended;
  cursor.buffered = static_cast<uint8_t>(buffered);
  cursor.nextRecord = 0;
  if (!buffered) return SnapshotRead::End;
  cursor.position = next.position;
  cursor.cluster = next.cluster;
  std::memcpy(cursor.records, bytes, buffered * 32);
  std::memcpy(record, cursor.records[cursor.nextRecord++], sizeof(cursor.records[0]));
  return SnapshotRead::Record;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  const bool mutating = (oflag & O_ACCMODE) != O_RDONLY || (oflag & (O_CREAT | O_TRUNC)) != 0;
  if (mutating) mutationFence.mutationAttempt();
  auto opened = SDCard.open(path, oflag);
  if (mutating && !opened) mutationFence.poison();
  return HalFile(std::make_unique<HalFile::Impl>(std::move(opened)));
}

HalFile HalStorage::openChecked(const char* path, const oflag_t oflag) {
  StorageLock lock;
  auto wrapper = makeUniqueNoThrow<HalFile::Impl>(FsFile{});
  if (!wrapper) return {};
  const bool mutating = (oflag & O_ACCMODE) != O_RDONLY || (oflag & (O_CREAT | O_TRUNC)) != 0;
  if (mutating) mutationFence.mutationAttempt();
  wrapper->file = SDCard.open(path, oflag);
  if (mutating && !wrapper->file) mutationFence.poison();
  wrapper->registerWritable();
  return HalFile(std::move(wrapper));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  StorageLock lock;
  if (path && SDCard.exists(path) && lookupPath(path) == PathStatus::Present) return false;
  mutationFence.mutationAttempt();
  const bool ok = SDCard.mkdir(path, pFlag);
  if (!ok) mutationFence.poison();
  return ok;
}

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

HalStorage::PathStatus HalStorage::lookupPath(const char* path) {
  StorageLock lock;
  if (!SDCard.ready() || !path || path[0] != '/') return PathStatus::Failed;
  const size_t length = strlen(path);
  auto components = makeUniqueNoThrow<char[]>(length + 1);
  if (!components) return PathStatus::Failed;
  memcpy(components.get(), path, length + 1);
  char* component = components.get() + 1;
  // Validate the entire path before reporting an earlier component missing.
  for (char* part = component; *part;) {
    char* separator = strchr(part, '/');
    if (separator) *separator = '\0';
    if (!validCheckedStorageComponent(part)) return PathStatus::Failed;
    if (!separator) break;
    *separator = '/';
    part = separator + 1;
    if (!*part) return PathStatus::Failed;
  }
  auto directory = SDCard.open("/");
  if (!directory || !directory.isDirectory()) return PathStatus::Failed;
  while (*component) {
    // Only this handle opts in; regular readers retain their existing behavior.
    if (!directory.enableStrictLookup()) return PathStatus::Failed;
    char* separator = strchr(component, '/');
    if (separator) *separator = '\0';
    FsFile entry;
    // The pinned SdFat patch carries long-filename read errors to the parent.
    if (!entry.open(&directory, component, O_RDONLY)) {
      const bool failed = directory.getError() != 0;
      const bool closed = directory.close();
      return failed || !closed ? PathStatus::Failed : PathStatus::Missing;
    }
    if (directory.getError() != 0 || !directory.close()) return PathStatus::Failed;
    if (!separator) return entry.close() ? PathStatus::Present : PathStatus::Failed;
    if (!entry.isDirectory()) return PathStatus::Failed;
    directory = std::move(entry);
    component = separator + 1;
    if (!*component) return PathStatus::Failed;
  }
  return directory.close() ? PathStatus::Present : PathStatus::Failed;
}

bool HalStorage::withStorageLock(bool (*operation)(void*), void* context) {
  if (!operation) return false;
  StorageLock lock;
  return operation(context);
}

bool HalStorage::lookupNumberedChildren(const char* path, const char* prefix, uint64_t& present) {
  StorageLock lock;
  if (!path || !prefix) return false;
  char name[64];
  const int length = snprintf(name, sizeof(name), "%s%02u", prefix, 63u);
  if (length < 0 || length >= static_cast<int>(sizeof(name)) || !validCheckedStorageComponent(name)) return false;
  const auto status = lookupPath(path);
  if (status == PathStatus::Failed) return false;
  if (status == PathStatus::Missing) {
    present = 0;
    return true;
  }
  auto directory = SDCard.open(path);
  if (!directory) return false;
  if (!directory.isDirectory() || !directory.enableStrictLookup()) {
    directory.close();
    return false;
  }
  uint64_t mask = 0;
  bool ok = true;
  for (unsigned i = 0; i < 64; ++i) {
    snprintf(name, sizeof(name), "%s%02u", prefix, i);
    FsFile entry;
    const bool found = entry.open(&directory, name, O_RDONLY);
    const bool failed = directory.getError() != 0;
    const bool closed = !found || entry.close();
    if (failed || !closed) {
      ok = false;
      break;
    }
    if (found) mask |= uint64_t{1} << i;
  }
  const bool closed = directory.close();
  if (!ok || !closed) return false;
  present = mask;
  return true;
}

bool HalStorage::distinctFirstClusters(const char* first, const char* second) {
  StorageLock lock;
  if (!first || !second || lookupPath(first) != PathStatus::Present || lookupPath(second) != PathStatus::Present)
    return false;
  auto left = SDCard.open(first, O_RDONLY);
  auto right = SDCard.open(second, O_RDONLY);
  const bool distinct = left && right && smallFilesHaveDifferentFirstClusters(left, right);
  const bool leftClosed = !left || left.close();
  const bool rightClosed = !right || right.close();
  return distinct && leftClosed && rightClosed;
}

bool HalStorage::distinctSettingsClusters(const char* first, const char* second) {
  StorageLock lock;
  if (!first || !second || lookupPath(first) != PathStatus::Present || lookupPath(second) != PathStatus::Present)
    return false;
  auto left = SDCard.open(first, O_RDONLY);
  auto right = SDCard.open(second, O_RDONLY);
  const bool distinct = left && right && settingsFilesHaveDifferentFirstClusters(left, right);
  const bool leftClosed = !left || left.close();
  const bool rightClosed = !right || right.close();
  return distinct && leftClosed && rightClosed;
}

bool HalStorage::settingsSingleClusterEligible(const char* path) {
  StorageLock lock;
  if (!path || lookupPath(path) != PathStatus::Present) return false;
  auto file = SDCard.open(path, O_RDONLY);
  const bool eligible = file && file.settingsSingleClusterChain();
  const bool closed = !file || file.close();
  return eligible && closed;
}

bool HalStorage::settingsChainEligible(const char* path) {
  StorageLock lock;
  if (!path || lookupPath(path) != PathStatus::Present) return false;
  auto file = SDCard.open(path, O_RDONLY);
  const bool eligible = file && settingsFileHasCheckedChain(file);
  const bool closed = !file || file.close();
  return eligible && closed;
}

bool HalStorage::distinctSettingsChains(const char* first, const char* second) {
  StorageLock lock;
  if (!first || !second || lookupPath(first) != PathStatus::Present || lookupPath(second) != PathStatus::Present)
    return false;
  auto left = SDCard.open(first, O_RDONLY);
  auto right = SDCard.open(second, O_RDONLY);
  const bool distinct = left && right && settingsFilesHaveDisjointChains(left, right);
  const bool leftClosed = !left || left.close();
  const bool rightClosed = !right || right.close();
  return distinct && leftClosed && rightClosed;
}

bool HalStorage::distinctMoveChains(const char* first, const char* second) {
  StorageLock lock;
  if (!first || !second || lookupPath(first) != PathStatus::Present || lookupPath(second) != PathStatus::Present)
    return false;
  auto left = SDCard.open(first, O_RDONLY);
  auto right = SDCard.open(second, O_RDONLY);
  const bool distinct = left && right && moveFilesHaveIndependentStorage(left, right);
  const bool leftClosed = !left || left.close();
  const bool rightClosed = !right || right.close();
  return distinct && leftClosed && rightClosed;
}

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  if (path && !SDCard.exists(path) && lookupPath(path) == PathStatus::Missing) return false;
  mutationFence.mutationAttempt();
  const bool ok = SDCard.remove(path);
  if (!ok) mutationFence.poison();
  return ok;
}
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_MUTATION_CALL(rename, oldPath, newPath);
}

bool HalStorage::replaceFile(const char* tmpPath, const char* path) {
  HAL_STORAGE_MUTATION_CALL(replaceFile, tmpPath, path);
}

bool HalStorage::rmdir(const char* path) {
  StorageLock lock;
  if (path && !SDCard.exists(path) && lookupPath(path) == PathStatus::Missing) return false;
  mutationFence.mutationAttempt();
  const bool ok = SDCard.rmdir(path);
  if (!ok) mutationFence.poison();
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  mutationFence.mutationAttempt();
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  if (!ok) mutationFence.poison();
  file = HalFile(std::make_unique<HalFile::Impl>(std::move(fsFile)));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) {
  StorageLock lock;
  if (path && !SDCard.exists(path) && lookupPath(path) == PathStatus::Missing) return false;
  mutationFence.mutationAttempt();
  const bool ok = SDCard.removeDir(path);
  if (!ok) mutationFence.poison();
  return ok;
}

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

#define HAL_FILE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;            \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, ...) \
  assert(impl != nullptr);                 \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  if (impl->hasWriterLease) {
    auto& fence = HalStorage::getInstance().mutationFence;
    fence.mutationAttempt();
    if (!impl->file.sync() || impl->file.getWriteError()) fence.poison();
  } else {
    impl->file.flush();
  }
}
bool HalFile::sync() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto& fence = HalStorage::getInstance().mutationFence;
  if (impl->hasWriterLease) fence.mutationAttempt();
  const bool ok = impl->file.sync();
  if (!ok || (impl->hasWriterLease && impl->file.getWriteError())) fence.poison();
  return ok;
}
size_t HalFile::getName(char* name, size_t len) { HAL_FILE_WRAPPED_CALL(getName, name, len); }
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, ); }  // already thread-safe, no need to wrap
uint32_t HalFile::modificationTime() {
  HalStorage::StorageLock lock;
  uint16_t date = 0;
  uint16_t time = 0;
  if (!impl || !impl->file.getModifyDateTime(&date, &time) || date == 0) return 0;
  return (static_cast<uint32_t>(date) << 16) | time;
}
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, offset); }
bool HalFile::truncate(const uint64_t length) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto& fence = HalStorage::getInstance().mutationFence;
  fence.mutationAttempt();
  const bool ok = impl->file.truncate(length);
  if (!ok) fence.poison();
  return ok;
}
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, ); }
size_t HalFile::write(const uint8_t* buf, size_t count) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto& fence = HalStorage::getInstance().mutationFence;
  fence.mutationAttempt();
  const size_t written = impl->file.write(buf, count);
  if (written != count || impl->file.getWriteError()) fence.poison();
  return written;
}
size_t HalFile::write(const void* buf, size_t count) { return write(static_cast<const uint8_t*>(buf), count); }
size_t HalFile::write(uint8_t b) { return write(&b, 1); }
bool HalFile::rename(const char* newPath) {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  auto& fence = HalStorage::getInstance().mutationFence;
  fence.mutationAttempt();
  const bool ok = impl->file.rename(newPath);
  if (!ok) fence.poison();
  return ok;
}
bool HalFile::isDirectory() const { HAL_FILE_FORWARD_CALL(isDirectory, ); }  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() { HAL_FILE_WRAPPED_CALL(rewindDirectory, ); }
bool HalFile::close() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  return impl->closeUnderLock();
}
HalFile HalFile::openNextFile() {
  HalStorage::StorageLock lock;
  assert(impl != nullptr);
  return HalFile(std::make_unique<Impl>(impl->file.openNextFile()));
}
bool HalFile::hasError() const {
  HalStorage::StorageLock lock;
  return !impl || impl->file.getError() != 0;
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
