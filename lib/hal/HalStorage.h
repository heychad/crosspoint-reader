#pragma once

#include <Print.h>
#include <common/FsApiConstants.h>  // for oflag_t
#include <freertos/semphr.h>

#include <memory>
#include <string>
#include <vector>

#include "StorageMutationFence.h"

class HalFile;

enum class UsbDriveState : uint8_t {
  Unsupported,
  WaitingForHost,
  Connected,
  Ejected,
  Disconnected,
  IoError,
};

class HalStorage {
 public:
  enum class PathStatus { Missing, Present, Failed };
  HalStorage();
  bool begin();
  bool ready() const;
  // X4 Pro only: whether the SDMMC driver has reported an I/O error since
  // mounting began. This is not a filesystem consistency check.
  bool observedSdmmcHealthy() const;
  // Process-local observations only: all writers must use this HAL, and raw
  // card access invalidates the fence. These APIs do not verify filesystem integrity.
  using MutationSnapshot = checkedscan::StorageMutationFence::Snapshot;
  bool beginMutationSnapshot(MutationSnapshot& snapshot);
  bool mutationSnapshotUnchanged(const MutationSnapshot& snapshot);
  // A marker can be held while a known writer is open; callers must serialize
  // their own changes and capture a new marker after each of them.
  using MutationMarker = checkedscan::StorageMutationFence::Marker;
  bool captureMutationMarker(MutationMarker& marker);
  bool mutationMarkerUnchanged(const MutationMarker& marker);
  enum class SnapshotFormat { Unknown, Fat, ExFat };
  SnapshotFormat snapshotFormat(const MutationSnapshot& snapshot);
  enum class SnapshotRead { Record, End, Changed, Failed };
  // Read a single FAT/exFAT directory record while holding the snapshot lock.
  SnapshotRead readDirectoryRecord(HalFile& directory, const MutationSnapshot& snapshot, uint8_t* record);
  struct DirectoryCursor {
    MutationSnapshot snapshot{};
    uint64_t position = 0;
    uint32_t cluster = 0;
    bool started = false;
    bool ended = false;
    bool failed = false;
    uint8_t buffered = 0;
    uint8_t nextRecord = 0;
    uint8_t records[8][32]{};
    char path[2049]{};
  };
  // Reopens and closes the directory within each locked batch of up to eight
  // records. Each returned record still checks the snapshot. A cursor is
  // bound to one path and snapshot and must live off a small task stack.
  SnapshotRead readDirectoryRecordAtPath(const char* path, const MutationSnapshot& snapshot, DirectoryCursor& cursor,
                                         uint8_t* record);
  // Stop the SD card for deep sleep: unmount, stop the SDMMC host, and release
  // the bus pads (no-op on SPI boards). Call only after all file users have
  // stopped; open HalFiles become invalid. A deep-sleep wake resets the MCU and
  // mounts storage again through begin().
  void prepareForDeepSleep();
  // USB Drive exclusively owns the SD card while active. Callers must stop
  // all filesystem work before beginUsbDrive(), then reboot after endUsbDrive().
  bool beginUsbDrive();
  bool disconnectUsbDriveHost();
  void endUsbDrive();
  UsbDriveState usbDriveState() const;
  bool usbDriveHostSuspended() const;
  std::vector<String> listFiles(const char* path = "/", int maxFiles = 200);
  // Read the entire file at `path` into a String. Returns empty string on failure.
  String readFile(const char* path);
  // Low-memory helpers:
  // Stream the file contents to a `Print` (e.g. `Serial`, or any `Print`-derived object).
  // Returns true on success, false on failure.
  bool readFileToStream(const char* path, Print& out, size_t chunkSize = 256);
  // Read up to `bufferSize-1` bytes into `buffer`, null-terminating it. Returns bytes read.
  size_t readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes = 0);
  // Read the whole file at `path` into `out`. Fails (without allocating) on
  // missing, directory, empty, above-`cap`, or short-read files.
  bool readFileToString(const char* moduleName, const std::string& path, size_t cap, std::string& out);
  // Write a string to `path` on the SD card. Overwrites existing file.
  // Returns true on success.
  bool writeFile(const char* path, const String& content);
  // Ensure a directory exists, creating it if necessary. Returns true on success.
  bool ensureDirectoryExists(const char* path);

  HalFile open(const char* path, const oflag_t oflag = O_RDONLY);
  // Allocate the wrapper before opening or creating the file; returns an empty
  // handle if that allocation fails. Other I/O failures also return empty.
  HalFile openChecked(const char* path, const oflag_t oflag = O_RDONLY);
  bool mkdir(const char* path, const bool pFlag = true);
  bool exists(const char* path);
  // Missing alone does not authorize recovery writes on FAT or exFAT.
  PathStatus lookupPath(const char* path);
  // Check exactly 64 prefix+two-digit child names under one directory.
  // False leaves the output unchanged; a missing directory returns an empty mask.
  bool lookupNumberedChildren(const char* directory, const char* prefix, uint64_t& present);
  // Execute a bounded transaction under the same recursive lock used by all
  // storage operations. The callback must not block waiting for another task
  // which needs storage. This only serializes operations through HalStorage.
  bool withStorageLock(bool (*operation)(void*), void* context);
  // Check distinct single-cluster chains for two regular files with allocated
  // lengths from 1 to 512 bytes. Longer or unreadable chains fail closed.
  // Caller must hold withStorageLock across the transaction; this does not
  // validate the entire filesystem.
  bool distinctFirstClusters(const char* first, const char* second);
  // Settings snapshots require distinct, checked single-cluster allocations;
  // each file is limited to 32 KiB and must fit its volume's cluster size.
  bool distinctSettingsClusters(const char* first, const char* second);
  bool settingsSingleClusterEligible(const char* path);
  // These checks cover up to 64 allocated clusters per settings file; they do
  // not establish ownership against other files on a damaged filesystem.
  bool settingsChainEligible(const char* path);
  bool distinctSettingsChains(const char* first, const char* second);
  // Distinct entries and independent bounded chains, including empty files.
  bool distinctMoveChains(const char* first, const char* second);
  bool remove(const char* path);
  bool rename(const char* oldPath, const char* newPath);
  // Move a fully written temp file over `path`, replacing any existing file.
  bool replaceFile(const char* tmpPath, const char* path);
  bool rmdir(const char* path);

  bool openFileForRead(const char* moduleName, const char* path, HalFile& file);
  bool openFileForRead(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForRead(const char* moduleName, const String& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const char* path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const std::string& path, HalFile& file);
  bool openFileForWrite(const char* moduleName, const String& path, HalFile& file);
  bool removeDir(const char* path);

  static HalStorage& getInstance() { return instance; }

  class StorageLock;  // private class, used internally

 private:
  friend class HalFile;
  static HalStorage instance;
  checkedscan::StorageMutationFence mutationFence;

  bool initialized = false;
  SemaphoreHandle_t storageMutex = nullptr;
};

#define Storage HalStorage::getInstance()

class HalFile : public Print {
  friend class HalStorage;
  class Impl;
  std::unique_ptr<Impl> impl;
  explicit HalFile(std::unique_ptr<Impl> impl);

 public:
  HalFile();
  ~HalFile();
  HalFile(HalFile&&);
  HalFile& operator=(HalFile&&);
  HalFile(const HalFile&) = delete;
  HalFile& operator=(const HalFile&) = delete;

  void flush();
  // Unlike flush(), report the underlying storage synchronization result.
  bool sync();
  size_t getName(char* name, size_t len);
  size_t size();
  size_t fileSize();
  uint64_t fileSize64();
  uint32_t modificationTime();
  bool seek(size_t pos);
  bool seek64(uint64_t pos);
  bool seekCur(int64_t offset);
  bool seekSet(size_t offset);
  bool truncate(uint64_t length);
  int available() const;
  size_t position() const;
  int read(void* buf, size_t count);
  int read();  // read a single byte
  size_t write(const uint8_t* buf, size_t count) override;
  size_t write(const void* buf, size_t count);
  size_t write(uint8_t b) override;
  bool rename(const char* newPath);
  bool isDirectory() const;
  void rewindDirectory();
  bool close();
  HalFile openNextFile();
  // A directory iteration can fail rather than reach the end.
  bool hasError() const;
  bool isOpen() const;
  operator bool() const;
};

// Downstream code must use Storage instead of SdMan
#ifdef SdMan
#undef SdMan
#endif
