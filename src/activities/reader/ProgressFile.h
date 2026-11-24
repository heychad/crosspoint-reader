#pragma once

#include <HalStorage.h>
#include <Logging.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace ProgressFile {

// Stage the native progress record before replacing it (see issue #2275).
// Failed writes, syncs, or closes leave the existing position in place. FAT and
// exFAT still require separate remove and rename operations: losing power
// between them can leave the reader without a canonical position file.
inline bool writeAtomic(const std::string& cachePath, const uint8_t* data, size_t len) {
  const std::string finalPath = cachePath + "/progress.bin";
  const std::string tmpPath = cachePath + "/progress.bin.tmp";

  {
    HalFile f;
    if (!Storage.openFileForWrite("PRG", tmpPath, f)) {
      LOG_ERR("PRG", "Could not open temp progress file for write: %s", tmpPath.c_str());
      return false;
    }
    const size_t written = f.write(data, len);
    if (written != len) {
      LOG_ERR("PRG", "Short write saving progress to %s: %u/%u bytes", tmpPath.c_str(), (unsigned)written,
              (unsigned)len);
      return false;
    }
    const bool synced = f.sync();
    const bool healthy = !f.hasError();
    const bool closed = f.close();
    if (!synced || !healthy || !closed) {
      LOG_ERR("PRG", "Failed to finish writing temp progress file: %s", tmpPath.c_str());
      return false;
    }
  }

  // SdFat requires the destination to be absent for rename.
  Storage.remove(finalPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), finalPath.c_str())) {
    LOG_ERR("PRG", "Failed to rename temp progress into place: %s", finalPath.c_str());
    return false;
  }
  return true;
}

}  // namespace ProgressFile
