#include <HalMemory.h>
#include <OtaUpdater.h>
#include <PersonalImage.h>
#include <PersonalUpdateTransport.h>
#include <esp_ota_ops.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

#include "ImageFixture.h"

namespace {
bool failVerifierAllocation = false;
struct Environment {
  bool available = true, releaseOk = true, releaseMissing = false, imageOk = true, psram = true,
       partitionPresent = true, open = false, selected = false;
  int beginError = 0, endError = 0, bootError = 0, failWrite = -1;
  int begins = 0, writes = 0, ends = 0, aborts = 0, boots = 0, releaseCalls = 0, imageCalls = 0;
  size_t width = 17, beginSize = 0;
  uint16_t chip = 9;
  esp_partition_t partition{2048};
  std::vector<uint8_t> image, written;
  std::string metadata;
} env;
const char* IMAGE_URL =
    "https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.4-x4pro.bin";
std::string metadata() {
  return std::string(
             R"({"tag_name":"2.0.0-chad.4","draft":false,"prerelease":false,"assets":[{"name":"crosspoint-2.0.0-chad.4-x4pro.bin","state":"uploaded","size":)") +
         std::to_string(env.image.size()) + R"(,"digest":")" + digestText(imageDigest(env.image)) +
         R"(","browser_download_url":")" + IMAGE_URL + R"("}]})";
}
void seed() {
  failVerifierAllocation = false;
  fakehash::failStart = fakehash::failUpdate = fakehash::failFinish = false;
  env = {};
  env.image = imageFixture();
  env.metadata = metadata();
}
bool send(const uint8_t* data, size_t size, personalupdate::UpdateSink sink, void* context) {
  for (size_t offset = 0; offset < size; offset += env.width)
    if (!sink(context, data + offset, std::min(env.width, size - offset))) return false;
  return true;
}
class OtaPipeline : public testing::Test {
 protected:
  void SetUp() override { seed(); }
};
void expectNoBoot() {
  EXPECT_FALSE(env.selected);
  EXPECT_EQ(env.boots, 0);
  EXPECT_FALSE(env.open);
}
}  // namespace

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (failVerifierAllocation && size == sizeof(personalupdate::ImageVerifier)) return nullptr;
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
void HalMemory::PsramDeleter::operator()(uint8_t* value) const { std::free(value); }
HalMemory::PsramBuffer HalMemory::allocatePsram(size_t bytes) {
  return PsramBuffer(env.psram ? static_cast<uint8_t*>(std::malloc(bytes)) : nullptr);
}
bool personalupdate::transportAvailable() { return env.available; }
personalupdate::ReleaseFetch personalupdate::fetchRelease(UpdateSink sink, void* context) {
  ++env.releaseCalls;
  if (env.releaseMissing) return ReleaseFetch::NotFound;
  return env.releaseOk &&
                 send(reinterpret_cast<const uint8_t*>(env.metadata.data()), env.metadata.size(), sink, context)
             ? ReleaseFetch::Success
             : ReleaseFetch::Failure;
}
bool personalupdate::fetchImage(const char* url, UpdateSink sink, void* context) {
  ++env.imageCalls;
  EXPECT_STREQ(url, IMAGE_URL);
  return send(env.image.data(), env.image.size(), sink, context) && env.imageOk;
}
namespace firmware_flash {
uint16_t runningPartitionChipId() { return env.chip; }
}  // namespace firmware_flash
const esp_partition_t* esp_ota_get_next_update_partition(const void*) {
  return env.partitionPresent ? &env.partition : nullptr;
}
esp_err_t esp_ota_begin(const esp_partition_t* partition, size_t bytes, esp_ota_handle_t* handle) {
  EXPECT_EQ(partition, &env.partition);
  EXPECT_FALSE(env.open);
  ++env.begins;
  env.beginSize = bytes;
  if (env.beginError) return env.beginError;
  env.open = true;
  *handle = 42;
  return ESP_OK;
}
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void* data, size_t size) {
  EXPECT_EQ(handle, 42u);
  EXPECT_TRUE(env.open);
  if (env.writes++ == env.failWrite) return -1;
  const auto* bytes = static_cast<const uint8_t*>(data);
  env.written.insert(env.written.end(), bytes, bytes + size);
  return ESP_OK;
}
esp_err_t esp_ota_abort(esp_ota_handle_t handle) {
  EXPECT_EQ(handle, 42u);
  EXPECT_TRUE(env.open);
  env.open = false;
  ++env.aborts;
  return ESP_OK;
}
esp_err_t esp_ota_end(esp_ota_handle_t handle) {
  EXPECT_EQ(handle, 42u);
  EXPECT_TRUE(env.open);
  env.open = false;
  ++env.ends;
  return env.endError;
}
esp_err_t esp_ota_set_boot_partition(const esp_partition_t* partition) {
  EXPECT_EQ(partition, &env.partition);
  EXPECT_FALSE(env.open);
  ++env.boots;
  if (env.bootError) return env.bootError;
  env.selected = true;
  return ESP_OK;
}
const char* esp_err_to_name(esp_err_t) { return "injected fault"; }

TEST_F(OtaPipeline, PositiveSplitStreamsSelectBootOnlyAfterExactWritesAndSdkEnd) {
  for (size_t width : {1u, 2u, 7u, 13u, 14u, 15u, 17u, 128u, 129u}) {
    seed();
    env.width = width;
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    struct Progress {
      OtaUpdater* updater;
      std::vector<size_t> counts;
    } progress{&updater, {}};
    EXPECT_EQ(updater.installUpdate(
                  [](void* context) {
                    auto& value = *static_cast<Progress*>(context);
                    value.counts.push_back(value.updater->getProcessedSize());
                  },
                  &progress),
              OtaUpdater::OK);
    EXPECT_EQ(env.written, env.image);
    EXPECT_EQ(env.beginSize, env.image.size());
    EXPECT_EQ(env.begins, 1);
    EXPECT_EQ(env.ends, 1);
    EXPECT_EQ(env.boots, 1);
    EXPECT_EQ(env.aborts, 0);
    EXPECT_TRUE(env.selected);
    EXPECT_FALSE(env.open);
    EXPECT_TRUE(std::is_sorted(progress.counts.begin(), progress.counts.end()));
    ASSERT_FALSE(progress.counts.empty());
    EXPECT_EQ(progress.counts.back(), env.image.size());
    EXPECT_LE(progress.counts.size(), 101u);
  }
}
TEST_F(OtaPipeline, ClosedGateAndFailedRechecksClearPriorOffers) {
  OtaUpdater updater;
  ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  ASSERT_TRUE(updater.isUpdateNewer());
  env.available = false;
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::UNVERIFIED_TRANSPORT_ERROR);
  EXPECT_EQ(updater.getOtaSize(), 0u);
  EXPECT_TRUE(updater.getLatestVersion().empty());
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::UNVERIFIED_TRANSPORT_ERROR);
  env.available = true;
  EXPECT_EQ(updater.installUpdate(), OtaUpdater::UPDATE_OLDER_ERROR);
  EXPECT_EQ(env.begins, 0);
  expectNoBoot();
}
TEST_F(OtaPipeline, MetadataNetworkMemoryAndBodyLimitsNeverBeginFlash) {
  for (int failure = 0; failure < 5; ++failure) {
    seed();
    OtaUpdater updater;
    if (failure == 0)
      env.releaseOk = false;
    else if (failure == 1)
      env.psram = false;
    else if (failure == 2)
      env.metadata = "broken";
    else if (failure == 3)
      env.metadata = std::string(65537, 'x');
    else
      env.partitionPresent = false;
    EXPECT_NE(updater.checkForUpdate(), OtaUpdater::OK);
    EXPECT_FALSE(updater.isUpdateNewer());
    EXPECT_EQ(updater.installUpdate(), OtaUpdater::UPDATE_OLDER_ERROR);
    EXPECT_EQ(env.begins, 0);
    expectNoBoot();
  }
}
TEST_F(OtaPipeline, MissingPartitionUnknownChipAndVerifierAllocationStopBeforeErase) {
  for (int failure = 0; failure < 4; ++failure) {
    seed();
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    if (failure == 0)
      env.partitionPresent = false;
    else if (failure == 1)
      env.chip = 0xffff;
    else if (failure == 2)
      failVerifierAllocation = true;
    else
      env.partition.size = 127;
    const auto result = updater.installUpdate();
    failVerifierAllocation = false;
    EXPECT_NE(result, OtaUpdater::OK);
    EXPECT_EQ(env.begins, 0);
    expectNoBoot();
  }
}
TEST_F(OtaPipeline, CorruptionTruncationAndExtraBytesAbortWithoutEndOrBoot) {
  for (int failure = 0; failure < 4; ++failure) {
    seed();
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    if (failure == 0)
      env.image.back() ^= 1;
    else if (failure == 1)
      env.image.pop_back();
    else if (failure == 2)
      env.image.push_back(1);
    else
      env.imageOk = false;
    EXPECT_NE(updater.installUpdate(), OtaUpdater::OK);
    EXPECT_EQ(env.aborts, 1);
    EXPECT_EQ(env.ends, 0);
    expectNoBoot();
  }
}
TEST_F(OtaPipeline, ValidDigestCannotAuthorizeWrongHeaderChipOrBoard) {
  for (int failure = 0; failure < 4; ++failure) {
    seed();
    if (failure == 0)
      env.image[0] = 0;
    else if (failure == 1)
      env.image[12] = 0;
    else if (failure == 2)
      std::fill(env.image.begin() + 40, env.image.end(), 0xa5);
    else
      env.image = imageFixture("CROSSPOINT-BOARD-V1:sticky;");
    env.metadata = metadata();
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    EXPECT_NE(updater.installUpdate(), OtaUpdater::OK);
    EXPECT_EQ(env.aborts, 1);
    EXPECT_EQ(env.ends, 0);
    if (failure < 2) EXPECT_EQ(env.writes, 0);
    expectNoBoot();
  }
}
TEST_F(OtaPipeline, EveryWriteFailureAbortsExactlyOnce) {
  for (int fault = 0; fault < 9; ++fault) {
    seed();
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    env.failWrite = fault;
    EXPECT_EQ(updater.installUpdate(), OtaUpdater::INTERNAL_UPDATE_ERROR);
    EXPECT_EQ(env.aborts, 1);
    EXPECT_EQ(env.ends, 0);
    expectNoBoot();
  }
}
TEST_F(OtaPipeline, HashFailuresCannotFinalizeOrSelectBoot) {
  for (int failure = 0; failure < 3; ++failure) {
    seed();
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    if (failure == 0)
      fakehash::failStart = true;
    else if (failure == 1)
      fakehash::failUpdate = true;
    else
      fakehash::failFinish = true;
    EXPECT_EQ(updater.installUpdate(), OtaUpdater::INTERNAL_UPDATE_ERROR);
    EXPECT_EQ(env.begins, failure ? 1 : 0);
    EXPECT_EQ(env.aborts, failure ? 1 : 0);
    EXPECT_EQ(env.ends, 0);
    expectNoBoot();
  }
}
TEST_F(OtaPipeline, SdkBeginEndAndBootFailuresKeepHandleOwnershipCorrect) {
  for (int failure = 0; failure < 3; ++failure) {
    seed();
    OtaUpdater updater;
    ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
    if (failure == 0)
      env.beginError = -1;
    else if (failure == 1)
      env.endError = -1;
    else
      env.bootError = -1;
    EXPECT_EQ(updater.installUpdate(), OtaUpdater::INTERNAL_UPDATE_ERROR);
    EXPECT_EQ(env.aborts, 0);
    EXPECT_EQ(env.ends, failure ? 1 : 0);
    EXPECT_EQ(env.boots, failure == 2 ? 1 : 0);
    EXPECT_FALSE(env.selected);
    EXPECT_FALSE(env.open);
  }
}

TEST_F(OtaPipeline, MissingReleaseClearsAnEarlierOfferWithoutStartingFlash) {
  OtaUpdater updater;
  ASSERT_EQ(updater.checkForUpdate(), OtaUpdater::OK);
  ASSERT_TRUE(updater.isUpdateNewer());
  env.releaseMissing = true;
  EXPECT_EQ(updater.checkForUpdate(), OtaUpdater::NO_UPDATE);
  EXPECT_FALSE(updater.isUpdateNewer());
  EXPECT_TRUE(updater.getLatestVersion().empty());
  EXPECT_EQ(updater.installUpdate(nullptr, nullptr), OtaUpdater::UPDATE_OLDER_ERROR);
  EXPECT_EQ(env.begins, 0);
  expectNoBoot();
}
