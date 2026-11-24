#include "ProgressFile.h"

#include <HalStorage.h>
#include <Logging.h>

namespace ProgressFile {
namespace {

struct ProgressSave {
  const std::string& cache;
  const uint8_t* data;
  size_t size;
};

bool canReuseTemp(const std::string& temp, const std::string& final) {
  using Status = HalStorage::PathStatus;
  if (!Storage.settingsSingleClusterEligible(temp.c_str())) return false;
  const std::string backup = final + ".bak";
  for (const std::string* path : {&final, &backup}) {
    const auto status = Storage.lookupPath(path->c_str());
    if (status == Status::Failed ||
        (status == Status::Present && !Storage.distinctMoveChains(temp.c_str(), path->c_str())))
      return false;
  }
  return true;
}

bool writeLocked(void* context) {
  const auto& save = *static_cast<ProgressSave*>(context);
  const std::string final = save.cache + "/progress.bin";
  const std::string temp = final + ".tmp";
  // Opening without truncation keeps an interrupted rename's shared clusters intact.
  auto f = Storage.openChecked(temp.c_str(), O_WRONLY | O_CREAT);
  if (!f) {
    LOG_ERR("PRG", "Could not open staged progress file");
    return false;
  }
  if (!f.isUnallocatedEmpty()) {
    const auto stagedSize = f.fileSize64();
    if (!f.close() || stagedSize == 0 || stagedSize > 10 || !canReuseTemp(temp, final)) {
      LOG_ERR("PRG", "Could not verify the existing staged progress file");
      return false;
    }
    f = Storage.openChecked(temp.c_str(), O_WRONLY | O_TRUNC);
    if (!f) return false;
  }
  const size_t written = f.write(save.data, save.size);
  if (written != save.size) {
    LOG_ERR("PRG", "Short staged progress write: %u of %u bytes", static_cast<unsigned>(written),
            static_cast<unsigned>(save.size));
    return false;
  }
  const bool synced = f.sync();
  const bool healthy = !f.hasError();
  const bool closed = f.close();
  if (!synced || !healthy || !closed) {
    LOG_ERR("PRG", "Could not finish staging progress");
    return false;
  }
  Storage.remove(final.c_str());
  if (!Storage.rename(temp.c_str(), final.c_str())) {
    LOG_ERR("PRG", "Failed to publish staged progress");
    return false;
  }
  return true;
}

}  // namespace

bool writeAtomic(const std::string& cache, const uint8_t* data, size_t size) {
  ProgressSave save{cache, data, size};
  return Storage.withStorageLock(writeLocked, &save);
}

}  // namespace ProgressFile
