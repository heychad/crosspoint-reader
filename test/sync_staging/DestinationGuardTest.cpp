#include <CredentialIntegrity.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "network/sync/DestinationGuard.h"

namespace {
constexpr char DEST[] = "/Authors/Example/Book.epub";
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool mutate(void*) {
  ++fake::epoch;
  return true;
}
uint64_t fnv(std::string_view text) {
  uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : text) h = (h ^ c) * 1099511628211ULL;
  return h;
}
class DestinationTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors"};
    fake::epoch = 0;
    fake::writers = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
  }
};
TEST_F(DestinationTest, DetectsKnownDestinationStateAndSidecars) {
  using namespace syncpublish;
  const auto native = std::hash<std::string>{}(std::string(DEST));
  EXPECT_EQ(native, std::hash<std::string_view>{}(DEST));
  const auto legacy = fnv(DEST);
  const auto crc = credential_integrity::crc32(DEST);
  const std::vector<std::string> paths = {DEST,
                                          std::string(DEST) + ".key",
                                          std::string(DEST) + ".meta.json",
                                          std::string(DEST) + ".part",
                                          "/.crosspoint/epub_" + std::to_string(native),
                                          "/.crosspoint/epub_" + std::to_string(legacy),
                                          "/.crosspoint/reading_stats/epub_" + std::to_string(native),
                                          "/.crosspoint/clippings/epub_" + std::to_string(native) + ".bin.migrate.bak",
                                          "/.crosspoint/bookmarks/Authors_Example_Book.json.bak.move-tmp",
                                          "/.crosspoint/bookmarks/epub_" + std::to_string(crc) + ".bin.tmp",
                                          "/.crosspoint/bookmarks/epub_" + std::to_string(native) + ".bin.move-tmp"};
  EXPECT_EQ(inspectDestinationState(storage, DEST, proceed, nullptr), DestinationGuardResult::Clear);
  for (const auto& path : paths) {
    fake::files[path] = std::make_shared<fake::File>();
    EXPECT_EQ(inspectDestinationState(storage, DEST, proceed, nullptr), DestinationGuardResult::Conflict) << path;
    fake::files.erase(path);
  }
}
TEST_F(DestinationTest, CancellationAndTrackedChangesNeverClaimClear) {
  using namespace syncpublish;
  EXPECT_EQ(inspectDestinationState(storage, DEST, cancel, nullptr), DestinationGuardResult::Cancelled);
  EXPECT_EQ(inspectDestinationState(storage, DEST, mutate, nullptr), DestinationGuardResult::Changed);
  EXPECT_EQ(inspectDestinationState(storage, "/.crosspoint/Book.epub", proceed, nullptr),
            DestinationGuardResult::Invalid);
  failAllocation = true;
  EXPECT_EQ(inspectDestinationState(storage, DEST, proceed, nullptr), DestinationGuardResult::Unavailable);
}
}  // namespace
