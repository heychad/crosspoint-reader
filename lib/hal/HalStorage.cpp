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

bool HalStorage::beginMutationSnapshot(MutationSnapshot& snapshot) {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!crosspoint_sdmmc_monitor_healthy()) mutationFence.poison();
#else
  mutationFence.poison();
#endif
  if (!SDCard.ready()) mutationFence.poison();
  return mutationFence.beginSnapshot(snapshot);
}

bool HalStorage::mutationSnapshotUnchanged(const MutationSnapshot& snapshot) {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!crosspoint_sdmmc_monitor_healthy()) mutationFence.poison();
#else
  mutationFence.poison();
#endif
  if (!SDCard.ready()) mutationFence.poison();
  return mutationFence.unchanged(snapshot);
}

bool HalStorage::monitoredStorageHealthy() {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!crosspoint_sdmmc_monitor_healthy()) mutationFence.poison();
#else
  mutationFence.poison();
#endif
  if (!SDCard.ready()) mutationFence.poison();
  return mutationFence.healthy();
}

bool HalStorage::captureMutationMarker(MutationMarker& marker) {
  StorageLock lock;
  return monitoredStorageHealthy() && mutationFence.captureMarker(marker);
}

bool HalStorage::mutationMarkerUnchanged(const MutationMarker& marker) {
  StorageLock lock;
  return monitoredStorageHealthy() && mutationFence.markerUnchanged(marker);
}

HalStorage::SnapshotFormat HalStorage::snapshotFormat(const MutationSnapshot& snapshot) {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!crosspoint_sdmmc_monitor_healthy()) mutationFence.poison();
  if (!SDCard.ready()) mutationFence.poison();
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

bool HalStorage::snapshotGeometry(const MutationSnapshot& snapshot, VolumeGeometry& out) {
  StorageLock lock;
  out = {};
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!mutationSnapshotUnchanged(snapshot)) return false;
  SDCardManager::VolumeGeometry geometry{};
  if (!SDCard.volumeGeometry(geometry) || !mutationSnapshotUnchanged(snapshot)) return false;
  if (geometry.type != FAT_TYPE_FAT16 && geometry.type != FAT_TYPE_FAT32 && geometry.type != FAT_TYPE_EXFAT)
    return false;
  out = {geometry.type,      geometry.deviceSectors, geometry.fatStart,
         geometry.dataStart, geometry.clusterCount,  geometry.clusterBytes};
  return true;
#else
  (void)snapshot;
  mutationFence.poison();
  return false;
#endif
}

bool HalStorage::readExFatSector(const MutationSnapshot& snapshot, ExFatSector kind, uint32_t sector, uint8_t* out) {
  StorageLock lock;
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  VolumeGeometry geometry{};
  if (!out || !snapshotGeometry(snapshot, geometry) || geometry.type != FAT_TYPE_EXFAT) return false;
  bool ok = false;
  switch (kind) {
    case ExFatSector::Metadata:
      if (sector >= geometry.deviceSectors) return false;
      ok = SDCard.syncReadExFatMetadataSector(sector, out);
      break;
    case ExFatSector::Fat:
      if (uint64_t(geometry.fatStart) + sector >= geometry.deviceSectors) return false;
      ok = SDCard.syncReadExFatFatSector(sector, out);
      break;
    case ExFatSector::Bitmap:
      if (uint64_t(geometry.dataStart) + sector >= geometry.deviceSectors ||
          sector >= (uint64_t(geometry.clusterCount) + 4095) / 4096)
        return false;
      ok = SDCard.syncReadExFatBitmapSector(sector, out);
      break;
    default:
      return false;
  }
  if (!ok) mutationFence.poison();
  return ok && mutationSnapshotUnchanged(snapshot);
#else
  (void)snapshot;
  (void)kind;
  (void)sector;
  (void)out;
  mutationFence.poison();
  return false;
#endif
}

void HalStorage::prepareForDeepSleep() {
  StorageLock lock;
  mutationFence.poison();
  SDCard.shutdown();
}

bool HalStorage::beginUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  mutationFence.poison();  // Raw USB access bypasses HAL mutation tracking.
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
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {
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
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!crosspoint_sdmmc_monitor_healthy()) mutationFence.poison();
#else
  mutationFence.poison();
#endif
  if (!SDCard.ready()) mutationFence.poison();
  if (!mutationFence.unchanged(snapshot)) return SnapshotRead::Changed;
  if (!record || !directory.impl || !directory.impl->file.isDirectory()) return SnapshotRead::Failed;
  const int count = directory.impl->file.read(record, 32);
#if defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO
  if (!crosspoint_sdmmc_monitor_healthy()) mutationFence.poison();
#endif
  if (!mutationFence.unchanged(snapshot)) return SnapshotRead::Changed;
  if (count == 0 && directory.impl->file.getError() == 0) return SnapshotRead::End;
  if (count != 32 || directory.impl->file.getError() != 0) {
    mutationFence.poison();
    return SnapshotRead::Failed;
  }
  return SnapshotRead::Record;
}

HalStorage::SnapshotRead HalStorage::readRootDirectoryRecord(const MutationSnapshot& snapshot,
                                                             RootDirectoryCursor& cursor, uint8_t* record) {
  StorageLock lock;
  if (!record || cursor.failed || !mutationSnapshotUnchanged(snapshot) ||
      (cursor.started && !mutationSnapshotUnchanged(cursor.snapshot))) {
    cursor.failed = true;
    return SnapshotRead::Changed;
  }
  auto root = SDCard.open("/", O_RDONLY);
  if (!root || !root.isDirectory()) {
    cursor.failed = true;
    return SnapshotRead::Failed;
  }
  if (cursor.started) {
    const fspos_t pos{cursor.position, cursor.cluster};
    root.fsetpos(&pos);
  }
  const int count = root.read(record, 32);
  const bool readError = root.getError() != 0 || (count != 0 && count != 32);
  fspos_t next{};
  if (!readError && count == 32) root.fgetpos(&next);
  const bool closed = root.close();
  if (!closed || readError) mutationFence.poison();
  if (!closed || readError || !mutationSnapshotUnchanged(snapshot)) {
    cursor.failed = true;
    return SnapshotRead::Failed;
  }
  if (count == 0) return SnapshotRead::End;
  if (!cursor.started) cursor.snapshot = snapshot;
  cursor.started = true;
  cursor.position = next.position;
  cursor.cluster = next.cluster;
  return SnapshotRead::Record;
}

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  const bool mutating = (oflag & O_ACCMODE) != O_RDONLY || (oflag & (O_CREAT | O_TRUNC)) != 0;
  if (mutating) mutationFence.mutationAttempt();
  auto opened = SDCard.open(path, oflag);
  if (mutating && !opened) mutationFence.poison();
  auto impl = makeUniqueNoThrow<HalFile::Impl>(std::move(opened));
  if (!impl) {
    if (!opened.close() || mutating) mutationFence.poison();
    return HalFile();
  }
  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) {
  StorageLock lock;
  // SdFat mkdir returns false for an existing final component. Avoid an
  // expected failed create only after a checked lookup confirms it exists.
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

bool HalStorage::remove(const char* path) {
  StorageLock lock;
  // A missing temporary file is ordinary cleanup. Do not issue a mutating
  // call when a checked lookup proves there is no entry to remove.
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
bool HalFile::captureReadCursor(ReadCursor& cursor) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file.isOpen() || impl->file.isWritable() || impl->file.isDirectory()) return false;
  fspos_t pos{};
  impl->file.fgetpos(&pos);
  cursor.position = pos.position;
  cursor.cluster = pos.cluster;
  return true;
}
bool HalFile::restoreReadCursor(const ReadCursor& cursor) {
  HalStorage::StorageLock lock;
  if (!impl || !impl->file.isOpen() || impl->file.isWritable() || impl->file.isDirectory() ||
      cursor.position > impl->file.fileSize())
    return false;
  const fspos_t pos{cursor.position, cursor.cluster};
  impl->file.fsetpos(&pos);
  return true;
}
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
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
