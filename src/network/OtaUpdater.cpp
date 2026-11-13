#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <Memory.h>
#include <HalMemory.h>
#include <esp_ota_ops.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"
#include "PersonalRelease.h"
#include "PersonalUpdateVersion.h"

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  otaSize = processedSize = totalSize = 0;
  expectedSha256.fill(0);
  if (!personalupdate::AUTHENTICATED_TRANSPORT) return UNVERIFIED_TRANSPORT_ERROR;
  if (board_tag::boardNameLen() != 5 || std::memcmp(board_tag::boardName(), "x4pro", 5)) return WRONG_DEVICE_ERROR;
  const auto* partition = esp_ota_get_next_update_partition(nullptr);
  if (!partition) return INTERNAL_UPDATE_ERROR;
  auto body = HalMemory::allocatePsram(personalupdate::MAX_RELEASE_BYTES + 1);
  auto release = makeUniqueNoThrow<personalupdate::Release>();
  if (!body || !release) {
    LOG_ERR("OTA", "OOM: bounded release metadata");
    return OOM_ERROR;
  }
  size_t received = 0;
  bool tooLarge = false;
  const bool ok = HttpDownloader::fetchUrl(personalupdate::RELEASE_API, [&](const uint8_t* data, size_t len) {
    if (len > personalupdate::MAX_RELEASE_BYTES - received) {
      tooLarge = true;
      return false;
    }
    if (len) std::memcpy(body.get() + received, data, len);
    received += len;
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return tooLarge ? JSON_PARSE_ERROR : HTTP_ERROR;
  }
  body[received] = 0;
  switch (personalupdate::parseRelease(reinterpret_cast<char*>(body.get()), received, partition->size, *release)) {
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

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!personalupdate::AUTHENTICATED_TRANSPORT) return UNVERIFIED_TRANSPORT_ERROR;
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  processedSize = 0;
  int lastReportedPct = -1;
  bool flashOk = true;
  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12)
  // and reject a wrong-MCU image before it overwrites the OTA partition.
  uint8_t hdr[14];
  size_t hdrLen = 0;
  bool wrongChip = false;
  // All S3 boards share a chip_id, so also scan the stream for the embedded
  // board tag (FirmwareBoardTag.h). An untagged image passes; a tag naming a
  // different board aborts the download. The wrong image may partially land in
  // the inactive OTA slot, but esp_ota_abort() below means it never becomes
  // the boot target.
  board_tag::Scanner tagScanner;
  const bool fetchOk = HttpDownloader::fetchUrl(otaUrl, [&](const uint8_t* data, size_t len) {
    if (hdrLen < sizeof(hdr)) {
      const size_t take = std::min(len, sizeof(hdr) - hdrLen);
      std::memcpy(hdr + hdrLen, data, take);
      hdrLen += take;
      if (hdrLen == sizeof(hdr)) {
        uint16_t imageChip;
        std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
        const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
        if (deviceChip != 0xFFFF && imageChip != deviceChip) {
          LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
          wrongChip = true;
          return false;  // abort the transfer
        }
      }
    }
    tagScanner.feed(data, len);
    if (tagScanner.mismatch()) {
      LOG_ERR("OTA", "wrong board: image=%s device=%.*s", tagScanner.foundName(),
              static_cast<int>(board_tag::boardNameLen()), board_tag::boardName());
      return false;  // abort the transfer
    }
    if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
      flashOk = false;
      return false;  // abort the transfer
    }
    processedSize += len;
    // Fire the callback only on whole-percent change. Per-chunk updates wake the
    // render task, whose framebuffer work contends with TLS on the internal arena,
    // and e-ink can't repaint faster than a percent tick anyway.
    if (onProgress && totalSize > 0) {
      const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
      if (pct != lastReportedPct) {
        lastReportedPct = pct;
        onProgress(ctx);
      }
    }
    return true;
  });

  if (wrongChip || tagScanner.mismatch()) {
    LOG_ERR("OTA", "Firmware install aborted: wrong device");
    esp_ota_abort(otaHandle);
    return WRONG_DEVICE_ERROR;
  }

  if (!fetchOk || !flashOk) {
    LOG_ERR("OTA", "Firmware install failed (%s)", flashOk ? "download" : "flash write");
    esp_ota_abort(otaHandle);
    return flashOk ? HTTP_ERROR : INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
