#include <Memory.h>
#include <gtest/gtest.h>

#include "network/sync/StoredReferenceGuard.h"

namespace {
using namespace syncpublish;
constexpr char DEST[] = "/Authors/Example/Book.epub";
bool proceed(void*) { return true; }
bool cancel(void*) { return false; }
bool mutate(void*) {
  ++fake::epoch;
  return true;
}
class ReferenceTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::directories = {"/", "/.crosspoint", "/Authors"};
    fake::epoch = fake::writers = 0;
    fake::failWrite = fake::failClose = fake::failRead = fake::failSync = fake::missingParent = failAllocation = false;
  }
  void put(const char* name, const std::string& data) {
    auto file = std::make_shared<fake::File>();
    file->bytes.assign(data.begin(), data.end());
    fake::files[name] = file;
  }
  ReferenceGuardResult check() { return inspectStoredReferences(storage, DEST, proceed, nullptr); }
};
TEST_F(ReferenceTest, ChecksStateAndRecentIncludingBackup) {
  EXPECT_EQ(check(), ReferenceGuardResult::Clear);
  put("/.crosspoint/state.json", R"({"openEpubPath":"/elsewhere/BOOK.epub"})");
  EXPECT_EQ(check(), ReferenceGuardResult::Conflict);
  put("/.crosspoint/state.json", R"({"openEpubPath":"/elsewhere/other.epub"})");
  EXPECT_EQ(check(), ReferenceGuardResult::Clear);
  put("/.crosspoint/recent.json.bak", R"({"books":[{"path":"/elsewhere/Book.epub"}]})");
  EXPECT_EQ(check(), ReferenceGuardResult::Conflict);
}
TEST_F(ReferenceTest, ChecksSleepPreferenceRecoveryArtifacts) {
  for (const char* path : {"/.crosspoint/state.json.sleep-bak", "/.crosspoint/state.json.sleep-tmp"}) {
    put(path, R"({"openEpubPath":"/Authors/Example/Book.epub"})");
    EXPECT_EQ(check(), ReferenceGuardResult::Conflict);
    EXPECT_EQ(inspectStoredSourceReferences(storage, DEST, proceed, nullptr), ReferenceGuardResult::Conflict);
    put(path, "{");
    EXPECT_EQ(check(), ReferenceGuardResult::Unavailable);
    fake::files.erase(path);
  }
}
TEST_F(ReferenceTest, RejectsAmbiguousOrInvalidData) {
  for (const char* json :
       {R"({"openEpubPath":"/x/Book.epub","openEpubPath":"/x/other.epub"})", R"({"openEpubPath":""}X)",
        R"({"openEpubPath":123})", R"({"openEpubP\u0061th":"/x/other.epub"})", "{"}) {
    put("/.crosspoint/state.json", json);
    EXPECT_EQ(check(), ReferenceGuardResult::Unavailable) << json;
  }
  put("/.crosspoint/state.json", R"({"openEpubPath":"/x/../other.epub"})");
  EXPECT_EQ(check(), ReferenceGuardResult::Conflict);
  put("/.crosspoint/recent.json", R"({"books":[{"path":"/x/Book.epub","path":"/x/other.epub"}]})");
  fake::files.erase("/.crosspoint/state.json");
  EXPECT_EQ(check(), ReferenceGuardResult::Unavailable);
  put("/.crosspoint/recent.json", R"({"books":[{"path":"/x/Book.epub"}],"books":[]})");
  EXPECT_EQ(check(), ReferenceGuardResult::Unavailable);
  put("/.crosspoint/recent.json", R"({"books":[{"path":"/x/other.epub"}]})");
  EXPECT_EQ(check(), ReferenceGuardResult::Clear);
}
TEST_F(ReferenceTest, StreamsAndFailsClosedOnReadOrMutation) {
  put("/.crosspoint/recent.json",
      "{\"ignored\":\"" + std::string(2048, 'a') + "\",\"books\":[{\"path\":\"/x/Book.epub\"}]}");
  EXPECT_EQ(check(), ReferenceGuardResult::Conflict);
  fake::failRead = true;
  EXPECT_EQ(check(), ReferenceGuardResult::Unavailable);
  fake::failRead = false;
  EXPECT_EQ(inspectStoredReferences(storage, DEST, cancel, nullptr), ReferenceGuardResult::Cancelled);
  EXPECT_EQ(inspectStoredReferences(storage, DEST, mutate, nullptr), ReferenceGuardResult::Changed);
  fake::writers = 1;
  EXPECT_EQ(check(), ReferenceGuardResult::Unavailable);
}
TEST_F(ReferenceTest, DistinctAsciiPrefixesDoNotConflictWithAccentedNames) {
  EXPECT_FALSE(referenceMayConflict("/Authors/José.epub", DEST));
  EXPECT_FALSE(referenceMayConflict("/Authors/Booké.epub", DEST));
  EXPECT_TRUE(referenceMayConflict("/Authors/BOOK.epub", DEST));
  EXPECT_FALSE(referenceMayConflict("/Authors/Été.epub", DEST));
  EXPECT_TRUE(referenceMayConflict("/Authors/été.epub", "/Books/ÉTÉ.epub"));
  EXPECT_FALSE(referenceMayConflict("/Authors/😀.epub", "/Books/😁.epub"));
  EXPECT_TRUE(referenceMayConflict(std::string("/Authors/J") + char(0xff) + ".epub", DEST));
  EXPECT_TRUE(referenceMayConflict("/Authors/BOO~1.EPU", DEST));
}
TEST_F(ReferenceTest, SourceCheckUsesWholePathAndStillRejectsAmbiguousReferences) {
  const auto checkSource = [&] { return inspectStoredSourceReferences(storage, DEST, proceed, nullptr); };
  put("/.crosspoint/state.json", R"({"openEpubPath":"/read/Book.epub"})");
  put("/.crosspoint/recent.json", R"({"books":[{"path":"/read/Book.epub"}]})");
  EXPECT_EQ(checkSource(), ReferenceGuardResult::Clear);
  put("/.crosspoint/state.json", R"({"openEpubPath":"/AUTHORS/EXAMPLE/book.epub"})");
  EXPECT_EQ(checkSource(), ReferenceGuardResult::Conflict);
  put("/.crosspoint/state.json", R"({"openEpubPath":"/read/Book.epub"})");
  put("/.crosspoint/recent.json.bak", R"({"books":[{"path":"/Authors/Example/Book.epub"}]})");
  EXPECT_EQ(checkSource(), ReferenceGuardResult::Conflict);
  fake::files.erase("/.crosspoint/recent.json.bak");
  put("/.crosspoint/state.json", R"({"openEpubPath":"/AUTHO~1/BOOK.EPU"})");
  EXPECT_EQ(checkSource(), ReferenceGuardResult::Conflict);
  EXPECT_TRUE(referenceMayReferTo("/Authors/Été.epub", "/authors/été.epub"));
  EXPECT_FALSE(referenceMayReferTo("/read/Book.epub", DEST));
  EXPECT_TRUE(referenceMayReferTo("/Authors/../Book.epub", DEST));
  put("/.crosspoint/state.json", R"({"openEpubPath":"/read/Book.epub"})");
  fake::failRead = true;
  EXPECT_EQ(checkSource(), ReferenceGuardResult::Unavailable);
  fake::failRead = false;
  EXPECT_EQ(inspectStoredSourceReferences(storage, DEST, mutate, nullptr), ReferenceGuardResult::Changed);
}
TEST_F(ReferenceTest, DeepExistingSourceUsesReferencePolicyRatherThanPublicationPolicy) {
  constexpr char deep[] = "/a/b/c/d/e/f/g/h/i/j/k/l/m/n/o/p/verify.epub";
  EXPECT_EQ(inspectStoredSourceReferences(storage, deep, proceed, nullptr), ReferenceGuardResult::Clear);
  EXPECT_EQ(inspectStoredReferences(storage, deep, proceed, nullptr), ReferenceGuardResult::Invalid);
  put("/.crosspoint/state.json", std::string("{\"openEpubPath\":\"") + deep + "\"}");
  EXPECT_EQ(inspectStoredSourceReferences(storage, deep, proceed, nullptr), ReferenceGuardResult::Conflict);
  put("/.crosspoint/state.json", R"({"openEpubPath":"/read/verify.epub"})");
  EXPECT_EQ(inspectStoredSourceReferences(storage, deep, proceed, nullptr), ReferenceGuardResult::Clear);
}
}  // namespace
