#include "OtaUpdater.h"

#include <HalMemory.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "PersonalImage.h"
#include "PersonalRelease.h"
#include "PersonalUpdateTransport.h"
#include "PersonalUpdateVersion.h"

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = processedSize = totalSize = 0;
  expectedSha256.fill(0);
  if (!personalupdate::transportAvailable()) return UNVERIFIED_TRANSPORT_ERROR;
  if (board_tag::boardNameLen() != 5 || std::memcmp(board_tag::boardName(), "x4pro", 5)) return WRONG_DEVICE_ERROR;
  const auto* partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition) return INTERNAL_UPDATE_ERROR;
  auto body = HalMemory::allocatePsram(personalupdate::MAX_RELEASE_BYTES + 1);
  auto release = makeUniqueNoThrow<personalupdate::Release>();
  if (!body || !release) {
    LOG_ERR("OTA", "OOM: bounded release metadata");
    return OOM_ERROR;
  }
  struct Download {
    uint8_t* body;
    size_t received = 0;
    bool tooLarge = false;
  } download{body.get()};
  const auto fetched = personalupdate::fetchRelease(
      [](void* context, const uint8_t* data, size_t len) {
        auto& value = *static_cast<Download*>(context);
        if (len > personalupdate::MAX_RELEASE_BYTES - value.received) {
          value.tooLarge = true;
          return false;
        }
        if (len) std::memcpy(value.body + value.received, data, len);
        value.received += len;
        return true;
      },
      &download);
  if (fetched == personalupdate::ReleaseFetch::NotFound) return NO_UPDATE;
  if (fetched != personalupdate::ReleaseFetch::Success) {
    LOG_ERR("OTA", "Release check fetch failed");
    return download.tooLarge ? JSON_PARSE_ERROR : HTTP_ERROR;
  }
  body[download.received] = 0;
  switch (
      personalupdate::parseRelease(reinterpret_cast<char*>(body.get()), download.received, partition->size, *release)) {
    case personalupdate::ReleaseResult::Invalid:
      LOG_ERR("OTA", "Invalid personal release metadata");
      return JSON_PARSE_ERROR;
    case personalupdate::ReleaseResult::NoMemory:
      LOG_ERR("OTA", "OOM: release JSON parser");
      return OOM_ERROR;
    case personalupdate::ReleaseResult::MissingAsset:
      return NO_UPDATE;
    case personalupdate::ReleaseResult::Valid:
      break;
  }
  latestVersion = release->version;
  otaUrl = release->url;
  otaSize = release->size;
  expectedSha256 = release->sha256;
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }
  if (board_tag::boardNameLen() != 5 || memcmp(board_tag::boardName(), "x4pro", 5) != 0) return false;
  return personalupdate::isNewer(latestVersion, CROSSPOINT_VERSION);
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

namespace {
OtaUpdater::OtaUpdaterError imageError(const personalupdate::ImageResult result) {
  switch (result) {
    case personalupdate::ImageResult::Verified:
      return OtaUpdater::OK;
    case personalupdate::ImageResult::WrongDevice:
      return OtaUpdater::WRONG_DEVICE_ERROR;
    case personalupdate::ImageResult::HashError:
      return OtaUpdater::INTERNAL_UPDATE_ERROR;
    case personalupdate::ImageResult::Pending:
    case personalupdate::ImageResult::InvalidMetadata:
    case personalupdate::ImageResult::InvalidHeader:
    case personalupdate::ImageResult::TooLong:
    case personalupdate::ImageResult::TooShort:
    case personalupdate::ImageResult::DigestMismatch:
      return OtaUpdater::INTEGRITY_ERROR;
  }
  return OtaUpdater::INTEGRITY_ERROR;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!personalupdate::transportAvailable()) return UNVERIFIED_TRANSPORT_ERROR;
  if (!isUpdateNewer()) return UPDATE_OLDER_ERROR;
  const auto* partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition) return INTERNAL_UPDATE_ERROR;
  if (otaSize < 24 || otaSize > partition->size) return INTEGRITY_ERROR;
  auto image = makeUniqueNoThrow<personalupdate::ImageVerifier>(otaSize, expectedSha256,
                                                                firmware_flash::runningPartitionChipId());
  if (!image) {
    LOG_ERR("OTA", "OOM: %u-byte image verifier", static_cast<unsigned>(sizeof(personalupdate::ImageVerifier)));
    return OOM_ERROR;
  }
  if (image->result() != personalupdate::ImageResult::Pending) return imageError(image->result());

  esp_ota_handle_t handle = 0;
  esp_err_t error = esp_ota_begin(partition, otaSize, &handle);
  if (error != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(error));
    return INTERNAL_UPDATE_ERROR;
  }
  bool handleOpen = true;
  ScopedCleanup abortOnFailure{[&] {
    if (handleOpen) esp_ota_abort(handle);
  }};
  processedSize = 0;
  totalSize = otaSize;
  struct Download {
    OtaUpdater* updater;
    personalupdate::ImageVerifier* image;
    esp_ota_handle_t handle;
    ProgressCallback callback;
    void* context;
    int lastPercent = -1;
    bool flashOk = true;
  } download{this, image.get(), handle, onProgress, ctx};
  const bool fetched = personalupdate::fetchImage(
      otaUrl.c_str(),
      [](void* context, const uint8_t* data, size_t length) {
        auto& value = *static_cast<Download*>(context);
        const size_t before = value.image->received();
        if (!value.image->feed(data, length)) return false;
        size_t offset = 0;
        const auto header = value.image->header();
        if (before < 14) {
          if (header.size() < 14) return true;
          // Hold a split header until magic and chip identity have both passed.
          if (esp_ota_write(value.handle, header.data(), header.size()) != ESP_OK) {
            value.flashOk = false;
            return false;
          }
          offset = header.size() - before;
        }
        if (length > offset && esp_ota_write(value.handle, data + offset, length - offset) != ESP_OK) {
          value.flashOk = false;
          return false;
        }
        value.updater->processedSize = value.image->received();
        if (value.callback) {
          const int percent =
              static_cast<int>(static_cast<uint64_t>(value.updater->processedSize) * 100 / value.updater->totalSize);
          if (percent != value.lastPercent) {
            value.lastPercent = percent;
            value.callback(value.context);
          }
        }
        return true;
      },
      &download);
  if (!download.flashOk) {
    LOG_ERR("OTA", "Inactive image write failed");
    return INTERNAL_UPDATE_ERROR;
  }
  if (image->result() != personalupdate::ImageResult::Pending) {
    LOG_ERR("OTA", "Image stream rejected: %u", static_cast<unsigned>(image->result()));
    return imageError(image->result());
  }
  if (!fetched) {
    LOG_ERR("OTA", "Image fetch failed");
    return HTTP_ERROR;
  }
  const auto verified = image->finish();
  if (verified != personalupdate::ImageResult::Verified) {
    LOG_ERR("OTA", "Image verification failed: %u", static_cast<unsigned>(verified));
    return imageError(verified);
  }
  error = esp_ota_end(handle);
  handleOpen = false;  // The SDK frees the handle on both success and failure.
  if (error != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(error));
    return INTERNAL_UPDATE_ERROR;
  }
  error = esp_ota_set_boot_partition(partition);
  if (error != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(error));
    return INTERNAL_UPDATE_ERROR;
  }
  LOG_INF("OTA", "Verified update completed");
  return OK;
}
