#include <CompletionJournalIO.h>
#include <HalStorage.h>
#include <Memory.h>
#include <gtest/gtest.h>

#include <string>

namespace cj = readingstats::completionjournal;
namespace {
constexpr char SLOT[] = "/.completion-journal/.completion-00";
class CompletionJournalIO : public testing::Test {
 protected:
  cj::Record record{};
  size_t size = 0;
  void SetUp() override {
    Storage.reset();
    cj::Book book{};
    cj::Global global{};
    book[0] = 5;
    global[0] = 3;
    ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_123", book, global, {}, record, size));
  }
};
}  // namespace

TEST_F(CompletionJournalIO, CreatesAndVerifiesWithoutOverwriting) {
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  EXPECT_EQ(cj::inspectSeal(0, record, size), cj::SealResult::Missing);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", SLOT, file));
  cj::Record readback{};
  ASSERT_EQ(file.read(readback.data(), readback.size()), static_cast<int>(size));
  ASSERT_TRUE(file.close());
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Occupied);
  ASSERT_TRUE(Storage.openFileForRead("TEST", SLOT, file));
  cj::Record after{};
  ASSERT_EQ(file.read(after.data(), after.size()), static_cast<int>(size));
  EXPECT_TRUE(file.close());
  EXPECT_EQ(readback, after);
}

TEST_F(CompletionJournalIO, PartialWritesArePreservedAndNeverOverwritten) {
  for (size_t length = 0; length < size; ++length) {
    Storage.reset();
    Storage.failNextWriteFor(SLOT, length);
    ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Uncertain) << length;
    ASSERT_TRUE(Storage.exists(SLOT));
    EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Occupied);
    HalFile file;
    ASSERT_TRUE(Storage.openFileForRead("TEST", SLOT, file));
    EXPECT_EQ(file.fileSize(), length);
    EXPECT_TRUE(file.close());
  }
}

TEST_F(CompletionJournalIO, UncertainSyncAndClosePreserveTheRecord) {
  Storage.failNextNewSyncFor(SLOT);
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Uncertain);
  EXPECT_TRUE(Storage.exists(SLOT));
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Occupied);
  Storage.reset();
  Storage.failNextNewCloseFor(SLOT);
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Uncertain);
  EXPECT_TRUE(Storage.exists(SLOT));
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Occupied);
}

TEST_F(CompletionJournalIO, RejectsInvalidInputAndFailedLookupBeforeCreating) {
  EXPECT_EQ(cj::createRecord(64, record, size), cj::CreateResult::Invalid);
  EXPECT_EQ(cj::createRecord(0, record, size - 1), cj::CreateResult::Invalid);
  EXPECT_FALSE(Storage.exists(SLOT));
  Storage.failReadsFor(SLOT);
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Uncertain);
  EXPECT_FALSE(Storage.exists(SLOT));
}

TEST_F(CompletionJournalIO, InspectionReturnsValidRecordsWithoutChangingThem) {
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Missing);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(output[0], 0xa5);
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Valid);
  EXPECT_EQ(outputSize, size);
  EXPECT_EQ(output, record);
  EXPECT_EQ(cj::inspectRecord(64, output, outputSize), cj::InspectResult::InvalidSlot);
  EXPECT_EQ(outputSize, size);
}

TEST_F(CompletionJournalIO, InspectionPreservesPartialAndDamagedRecords) {
  Storage.failNextWriteFor(SLOT, size / 2);
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Uncertain);
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Damaged);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(output[0], 0xa5);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", SLOT, file));
  EXPECT_EQ(file.fileSize(), size / 2);
  EXPECT_TRUE(file.close());
  EXPECT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Occupied);
}

TEST_F(CompletionJournalIO, InspectionReportsReadAndCloseFailuresAsUnavailable) {
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  Storage.failNextReadFor(SLOT);
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Unavailable);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(output[0], 0xa5);
  Storage.failNextCloseFor(SLOT);
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Unavailable);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(output[0], 0xa5);
  Storage.failReadsFor(SLOT);
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Unavailable);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(output[0], 0xa5);
}

TEST_F(CompletionJournalIO, ScanFindsOneCandidateAcrossAllSlots) {
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  unsigned slot = 99;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Empty);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
  EXPECT_EQ(output[0], 0xa5);
  ASSERT_EQ(cj::createRecord(63, record, size), cj::CreateResult::Created);
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::PreparedOnly);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
  ASSERT_EQ(cj::createSeal(63, record, size), cj::CreateResult::Created);
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Candidate);
  EXPECT_EQ(slot, 63u);
  EXPECT_EQ(outputSize, size);
  EXPECT_EQ(output, record);
}

TEST_F(CompletionJournalIO, ScanPreservesConflictingAndDamagedSlots) {
  ASSERT_EQ(cj::createRecord(1, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createRecord(63, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(1, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(63, record, size), cj::CreateResult::Created);
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  unsigned slot = 99;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Conflict);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
  EXPECT_EQ(cj::createRecord(1, record, size), cj::CreateResult::Occupied);
  EXPECT_EQ(cj::createRecord(63, record, size), cj::CreateResult::Occupied);
  Storage.reset();
  ASSERT_EQ(cj::createRecord(1, record, size), cj::CreateResult::Created);
  Storage.failNextWriteFor("/.completion-journal/.completion-63", size / 2);
  ASSERT_EQ(cj::createRecord(63, record, size), cj::CreateResult::Uncertain);
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Damaged);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
}

TEST_F(CompletionJournalIO, ScanDoesNotReturnACandidateWhenLaterLookupFails) {
  ASSERT_EQ(cj::createRecord(1, record, size), cj::CreateResult::Created);
  Storage.failReadsFor("/.completion-journal/.completion-63");
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  unsigned slot = 99;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Unavailable);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
}

TEST_F(CompletionJournalIO, AllocationFailureLeavesRecordAndOutputsUntouched) {
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  cj::Record output;
  output.fill(0xa5);
  size_t outputSize = 999;
  unsigned slot = 99;
  failNextBackupAllocation = true;
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Unavailable);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(outputSize, 999u);
  failNextBackupAllocation = true;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Unavailable);
  EXPECT_EQ(output[0], 0xa5);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
  EXPECT_EQ(cj::inspectRecord(0, output, outputSize), cj::InspectResult::Valid);
}

TEST_F(CompletionJournalIO, SealRequiresAnIdenticalVerifiedRecord) {
  constexpr char sealPath[] = "/.completion-journal/.cseal-00";
  EXPECT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Uncertain);
  EXPECT_FALSE(Storage.exists(sealPath));
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  cj::Book altered{};
  altered[0] = 5;
  altered[1] = 1;
  cj::Global global{};
  global[0] = 3;
  cj::Record different{};
  size_t differentSize = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_123", altered, global, {}, different, differentSize));
  EXPECT_EQ(cj::createSeal(0, different, differentSize), cj::CreateResult::Uncertain);
  EXPECT_FALSE(Storage.exists(sealPath));
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", sealPath, file));
  cj::Seal seal{};
  ASSERT_EQ(file.read(seal.data(), seal.size()), static_cast<int>(seal.size()));
  ASSERT_TRUE(file.close());
  EXPECT_TRUE(cj::matchesSeal(0, record, size, seal));
  EXPECT_EQ(cj::inspectSeal(0, record, size), cj::SealResult::Matching);
  EXPECT_EQ(cj::inspectSeal(0, different, differentSize), cj::SealResult::Damaged);
  EXPECT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Occupied);
}

TEST_F(CompletionJournalIO, InterruptedSealIsPreservedAndNotOverwritten) {
  constexpr char sealPath[] = "/.completion-journal/.cseal-00";
  for (size_t length = 0; length < cj::Seal{}.size(); ++length) {
    Storage.reset();
    ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
    Storage.failNextWriteFor(sealPath, length);
    EXPECT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Uncertain) << length;
    ASSERT_TRUE(Storage.exists(sealPath));
    EXPECT_EQ(cj::inspectSeal(0, record, size), cj::SealResult::Damaged);
    EXPECT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Occupied);
    HalFile file;
    ASSERT_TRUE(Storage.openFileForRead("TEST", sealPath, file));
    EXPECT_EQ(file.fileSize(), length);
    EXPECT_TRUE(file.close());
  }
}

TEST_F(CompletionJournalIO, SealInspectionPreservesReadErrors) {
  constexpr char sealPath[] = "/.completion-journal/.cseal-00";
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  Storage.failNextReadFor(sealPath);
  EXPECT_EQ(cj::inspectSeal(0, record, size), cj::SealResult::Unavailable);
  Storage.failNextCloseFor(sealPath);
  EXPECT_EQ(cj::inspectSeal(0, record, size), cj::SealResult::Unavailable);
  EXPECT_EQ(cj::inspectSeal(0, record, size), cj::SealResult::Matching);
  EXPECT_EQ(cj::inspectSeal(64, record, size), cj::SealResult::Invalid);
}

TEST_F(CompletionJournalIO, ScanDistinguishesPreparedRecordsFromOneSealedCandidate) {
  ASSERT_EQ(cj::createRecord(1, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createRecord(7, record, size), cj::CreateResult::Created);
  cj::Record output{};
  size_t outputSize = 0;
  unsigned slot = 99;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::PreparedOnly);
  ASSERT_EQ(cj::createSeal(7, record, size), cj::CreateResult::Created);
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Candidate);
  EXPECT_EQ(slot, 7u);
  EXPECT_EQ(outputSize, size);
  EXPECT_EQ(output, record);
}

TEST_F(CompletionJournalIO, ScanPreservesOrphanAndPartialSeals) {
  constexpr char sealPath[] = "/.completion-journal/.cseal-00";
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  Storage.failNextWriteFor(sealPath, 3);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Uncertain);
  cj::Record output{};
  size_t outputSize = 999;
  unsigned slot = 99;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Damaged);
  EXPECT_TRUE(Storage.exists(sealPath));
  EXPECT_TRUE(Storage.exists(SLOT));
  Storage.reset();
  ASSERT_EQ(cj::createRecord(0, record, size), cj::CreateResult::Created);
  ASSERT_EQ(cj::createSeal(0, record, size), cj::CreateResult::Created);
  ASSERT_TRUE(Storage.remove(SLOT));
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Damaged);
  EXPECT_TRUE(Storage.exists(sealPath));
}

TEST_F(CompletionJournalIO, ScanRejectsSealedRecordsWithInvalidStatisticsPayloads) {
  cj::Record invalid{};
  size_t invalidSize = 0;
  ASSERT_TRUE(cj::encode("/.crosspoint/reading_stats/epub_123", {}, {}, {}, invalid, invalidSize));
  ASSERT_EQ(cj::createRecord(0, invalid, invalidSize), cj::CreateResult::Created);
  EXPECT_EQ(cj::createSeal(0, invalid, invalidSize), cj::CreateResult::Invalid);
  cj::Seal seal{};
  ASSERT_TRUE(cj::encodeSeal(0, invalid, invalidSize, seal));
  HalFile file = Storage.open("/.completion-journal/.cseal-00", O_CREAT | O_EXCL | O_WRONLY);
  ASSERT_TRUE(file);
  ASSERT_EQ(file.write(seal.data(), seal.size()), seal.size());
  ASSERT_TRUE(file.close());
  cj::Record output{};
  size_t outputSize = 999;
  unsigned slot = 99;
  EXPECT_EQ(cj::scanRecords(output, outputSize, slot), cj::ScanResult::Damaged);
  EXPECT_EQ(outputSize, 999u);
  EXPECT_EQ(slot, 99u);
  EXPECT_TRUE(Storage.exists(SLOT));
}
