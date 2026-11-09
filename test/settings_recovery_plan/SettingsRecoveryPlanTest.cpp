#include <HalStorage.h>
#include <Memory.h>
#include <SettingsJsonAllocator.h>
#include <SettingsRecoveryPlan.h>
#include <SettingsRecoveryProbe.h>
#include <SettingsSafeWrite.h>
#include <SettingsSnapshotCopy.h>
#include <gtest/gtest.h>

#include <cstring>

using settingsrecovery::Candidate;
using settingsrecovery::Plan;
using settingsrecovery::plan;

TEST(SettingsSafeWrite, SavesFirstAndSubsequentChanges) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr char first[] = "{\"fontSize\":14}";
  constexpr char second[] = "{\"fontSize\":16}";
  EXPECT_TRUE(settingsrecovery::writeSettings(first, sizeof(first) - 1));
  EXPECT_TRUE(settingsrecovery::writeSettings(second, sizeof(second) - 1));
  EXPECT_TRUE(settingsrecovery::writeSettings(second, sizeof(second) - 1));
  HalFile file;
  ASSERT_TRUE(storage.openFileForRead("TEST", "/.crosspoint/settings.json", file));
  char result[sizeof(second)]{};
  ASSERT_EQ(file.read(result, sizeof(second) - 1), int(sizeof(second) - 1));
  EXPECT_STREQ(result, second);
  EXPECT_TRUE(file.close());
  EXPECT_FALSE(storage.exists("/.crosspoint/settings.json.tmp"));
  EXPECT_FALSE(storage.exists("/.crosspoint/settings.json.bak"));
}

TEST(SettingsSafeWrite, KeepsOldOrNewDocumentWhenOperationsFail) {
  auto& storage = HalStorage::getInstance();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto temporary = "/.crosspoint/settings.json.tmp";
  constexpr auto backup = "/.crosspoint/settings.json.bak";
  constexpr char oldValue[] = "{\"fontSize\":14}";
  constexpr char newValue[] = "{\"fontSize\":16}";
  for (int mode = 0; mode < 7; ++mode) {
    storage.reset();
    ASSERT_TRUE(settingsrecovery::writeSettings(oldValue, sizeof(oldValue) - 1));
    switch (mode) {
      case 0:
        storage.failNextWriteFor(temporary, 4);
        break;
      case 1:
        storage.failNextNewSyncFor(temporary);
        break;
      case 2:
        storage.failNextRenameFrom(primary);
        break;
      case 3:
        storage.failNextAliasRenameFrom(primary);
        break;
      case 4:
        storage.failNextRenameFrom(temporary);
        break;
      case 5:
        storage.failNextAliasRenameFrom(temporary);
        break;
      case 6:
        storage.failNextRemove(backup);
        break;
    }
    EXPECT_FALSE(settingsrecovery::writeSettings(newValue, sizeof(newValue) - 1)) << mode;
    bool found = false;
    for (const auto path : {primary, backup, temporary}) {
      HalFile file;
      if (!storage.openFileForRead("TEST", path, file)) continue;
      char data[sizeof(oldValue)]{};
      const int read = file.read(data, sizeof(oldValue) - 1);
      ASSERT_TRUE(file.close());
      if (read == int(sizeof(oldValue) - 1) && (!std::strcmp(data, oldValue) || !std::strcmp(data, newValue)))
        found = true;
    }
    EXPECT_TRUE(found) << mode;
    const bool retryExpected = mode == 1 || mode == 2 || mode == 4 || mode == 6;
    EXPECT_EQ(settingsrecovery::writeSettings(newValue, sizeof(newValue) - 1), retryExpected) << mode;
    if (mode == 4 || mode == 6) {
      HalFile archived;
      ASSERT_TRUE(storage.openFileForRead("TEST", "/.crosspoint/settings.recovered-01.bak", archived));
      char saved[sizeof(oldValue)]{};
      ASSERT_EQ(archived.read(saved, sizeof(oldValue) - 1), int(sizeof(oldValue) - 1));
      EXPECT_STREQ(saved, oldValue);
      EXPECT_TRUE(archived.close());
    }
  }
}

TEST(SettingsSafeWrite, RefusesInvalidAndPreexistingRecoveryFiles) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr char valid[] = "{\"fontSize\":14}";
  EXPECT_FALSE(settingsrecovery::writeSettings("{bad}", 5));
  HalFile backup;
  ASSERT_TRUE(storage.openFileForWrite("TEST", "/.crosspoint/settings.json.bak", backup));
  ASSERT_EQ(backup.write(valid, sizeof(valid) - 1), sizeof(valid) - 1);
  ASSERT_TRUE(backup.close());
  constexpr char different[] = "{\"fontSize\":16}";
  EXPECT_FALSE(settingsrecovery::writeSettings(different, sizeof(different) - 1));
  EXPECT_TRUE(storage.exists("/.crosspoint/settings.json.bak"));
  EXPECT_FALSE(storage.exists("/.crosspoint/settings.json"));
}

TEST(SettingsSafeWrite, RestoresMatchingSoleBackupWithoutDiscardingIt) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr char valid[] = "{\"fontSize\":14}";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", "/.crosspoint/settings.json.bak", file));
  ASSERT_EQ(file.write(valid, sizeof(valid) - 1), sizeof(valid) - 1);
  ASSERT_TRUE(file.close());
  ASSERT_TRUE(settingsrecovery::writeSettings(valid, sizeof(valid) - 1));
  for (const auto path : {"/.crosspoint/settings.json", "/.crosspoint/settings.recovered-01.bak"}) {
    ASSERT_TRUE(storage.openFileForRead("TEST", path, file));
    char bytes[sizeof(valid)]{};
    ASSERT_EQ(file.read(bytes, sizeof(valid) - 1), int(sizeof(valid) - 1));
    EXPECT_STREQ(bytes, valid);
    EXPECT_TRUE(file.close());
  }
  EXPECT_FALSE(storage.exists("/.crosspoint/settings.json.bak"));
  EXPECT_FALSE(storage.exists("/.crosspoint/settings.json.tmp"));
}

TEST(SettingsSafeWrite, RetryDoesNotOverwriteAnEarlierRecoveryCopy) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr char oldValue[] = "{\"fontSize\":14}";
  constexpr char newValue[] = "{\"fontSize\":16}";
  constexpr char older[] = "{\"fontSize\":12}";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", "/.crosspoint/settings.recovered-01.bak", file));
  ASSERT_EQ(file.write(older, sizeof(older) - 1), sizeof(older) - 1);
  ASSERT_TRUE(file.close());
  ASSERT_TRUE(settingsrecovery::writeSettings(oldValue, sizeof(oldValue) - 1));
  storage.failNextRenameFrom("/.crosspoint/settings.json.tmp");
  ASSERT_FALSE(settingsrecovery::writeSettings(newValue, sizeof(newValue) - 1));
  ASSERT_TRUE(settingsrecovery::writeSettings(newValue, sizeof(newValue) - 1));
  for (const auto [path, expected] : {std::pair{"/.crosspoint/settings.recovered-01.bak", older},
                                      std::pair{"/.crosspoint/settings.recovered-02.bak", oldValue}}) {
    ASSERT_TRUE(storage.openFileForRead("TEST", path, file));
    char bytes[sizeof(older)]{};
    ASSERT_EQ(file.read(bytes, sizeof(older) - 1), int(sizeof(older) - 1));
    EXPECT_STREQ(bytes, expected);
    EXPECT_TRUE(file.close());
  }
}

TEST(SettingsBootPlan, SelectsReadableOldSettingsWithoutAllowingAmbiguousWrites) {
  using settingsrecovery::BootCandidate;
  using settingsrecovery::bootPlan;
  constexpr Candidate states[] = {Candidate::Missing, Candidate::JsonObject, Candidate::Invalid,
                                  Candidate::Unavailable};
  for (auto primary : states) {
    for (auto backup : states) {
      for (auto temporary : states) {
        const auto actual = bootPlan(primary, backup, temporary);
        const bool clean =
            primary == Candidate::Missing && backup == Candidate::Missing && temporary == Candidate::Missing;
        const bool primaryClean =
            primary == Candidate::JsonObject && backup == Candidate::Missing && temporary == Candidate::Missing;
        EXPECT_EQ(actual.allowSave, clean || primaryClean);
        const auto expected = clean                              ? BootCandidate::Defaults
                              : primary == Candidate::JsonObject ? BootCandidate::Primary
                              : primary == Candidate::Missing && backup == Candidate::JsonObject ? BootCandidate::Backup
                                                                                                 : BootCandidate::None;
        EXPECT_EQ(actual.candidate, expected);
      }
    }
  }
}

TEST(SettingsRecoveryPlan, DistinguishesCleanStartPrimaryAndSoleBackup) {
  EXPECT_EQ(plan(Candidate::Missing, Candidate::Missing, Candidate::Missing), Plan::FirstRun);
  EXPECT_EQ(plan(Candidate::JsonObject, Candidate::Missing, Candidate::Missing), Plan::InspectPrimary);
  EXPECT_EQ(plan(Candidate::Missing, Candidate::JsonObject, Candidate::Missing), Plan::InspectBackup);
}

TEST(SettingsRecoveryPlan, PreservesAmbiguousOrUnreadableArtifacts) {
  constexpr Candidate states[] = {Candidate::Missing, Candidate::JsonObject, Candidate::Invalid,
                                  Candidate::Unavailable};
  for (auto primary : states) {
    for (auto backup : states) {
      for (auto temporary : states) {
        const bool clean =
            primary == Candidate::Missing && backup == Candidate::Missing && temporary == Candidate::Missing;
        const bool primaryOnly =
            primary == Candidate::JsonObject && backup == Candidate::Missing && temporary == Candidate::Missing;
        const bool backupOnly =
            primary == Candidate::Missing && backup == Candidate::JsonObject && temporary == Candidate::Missing;
        if (!clean && !primaryOnly && !backupOnly) EXPECT_EQ(plan(primary, backup, temporary), Plan::NeedsReview);
      }
    }
  }
}

TEST(SettingsRecoveryProbe, ReadsCandidatesWithoutChangingTheirContents) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.json.bak";
  constexpr auto temporary = "/.crosspoint/settings.json.tmp";
  EXPECT_EQ(settingsrecovery::probe(primary, backup, temporary).decision(), Plan::FirstRun);
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  const char valid[] = "{\"fontSize\":14}";
  ASSERT_EQ(file.write(valid, sizeof(valid) - 1), sizeof(valid) - 1);
  ASSERT_TRUE(file.close());
  auto snapshot = settingsrecovery::probe(primary, backup, temporary);
  EXPECT_EQ(snapshot.primary, Candidate::JsonObject);
  EXPECT_EQ(snapshot.decision(), Plan::InspectPrimary);
  ASSERT_TRUE(storage.openFileForWrite("TEST", temporary, file));
  const char broken[] = "{broken";
  ASSERT_EQ(file.write(broken, sizeof(broken) - 1), sizeof(broken) - 1);
  ASSERT_TRUE(file.close());
  snapshot = settingsrecovery::probe(primary, backup, temporary);
  EXPECT_EQ(snapshot.temporary, Candidate::Invalid);
  EXPECT_EQ(snapshot.decision(), Plan::NeedsReview);
  ASSERT_TRUE(storage.openFileForRead("TEST", primary, file));
  char after[sizeof(valid)]{};
  ASSERT_EQ(file.read(after, sizeof(valid) - 1), static_cast<int>(sizeof(valid) - 1));
  EXPECT_STREQ(after, valid);
  EXPECT_TRUE(file.close());
  EXPECT_TRUE(storage.exists(temporary));
}

TEST(SettingsRecoveryProbe, ReadCloseAndAllocationFailuresNeverAppearMissing) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto path = "/.crosspoint/settings.json";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", path, file));
  const char value[] = "{}";
  ASSERT_EQ(file.write(value, sizeof(value) - 1), sizeof(value) - 1);
  ASSERT_TRUE(file.close());
  storage.failNextReadFor(path);
  EXPECT_EQ(settingsrecovery::probe(path, "/missing.bak", "/missing.tmp").primary, Candidate::Unavailable);
  storage.failNextCloseFor(path);
  EXPECT_EQ(settingsrecovery::probe(path, "/missing.bak", "/missing.tmp").primary, Candidate::Unavailable);
  failNextBackupAllocation = true;
  EXPECT_EQ(settingsrecovery::probe(path, "/missing.bak", "/missing.tmp").primary, Candidate::Unavailable);
  EXPECT_TRUE(storage.exists(path));
}

TEST(SettingsRecoveryProbe, BootDecisionPreservesInterruptedSaveArtifacts) {
  using settingsrecovery::BootCandidate;
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.json.bak";
  constexpr auto temporary = "/.crosspoint/settings.json.tmp";
  HalFile file;
  auto create = [&](const char* path, const char* content) {
    ASSERT_TRUE(storage.openFileForWrite("TEST", path, file));
    const size_t length = std::strlen(content);
    ASSERT_EQ(file.write(content, length), length);
    ASSERT_TRUE(file.close());
  };
  create(backup, "{\"fontSize\":14}");
  create(temporary, "{broken");
  auto snapshot = settingsrecovery::probe(primary, backup, temporary);
  EXPECT_EQ(snapshot.bootDecision().candidate, BootCandidate::Backup);
  EXPECT_FALSE(snapshot.bootDecision().allowSave);
  create(primary, "{\"fontSize\":16}");
  snapshot = settingsrecovery::probe(primary, backup, temporary);
  EXPECT_EQ(snapshot.bootDecision().candidate, BootCandidate::Primary);
  EXPECT_FALSE(snapshot.bootDecision().allowSave);
  storage.failNextReadFor(primary);
  snapshot = settingsrecovery::probe(primary, backup, temporary);
  EXPECT_EQ(snapshot.bootDecision().candidate, BootCandidate::None);
  EXPECT_FALSE(snapshot.bootDecision().allowSave);
  EXPECT_TRUE(storage.exists(primary));
  EXPECT_TRUE(storage.exists(backup));
  EXPECT_TRUE(storage.exists(temporary));
}

TEST(SettingsSnapshotCopy, CreatesVerifiedCopyWithoutOverwritingExistingBackup) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  const std::string content = "{\"data\":\"" + std::string(2268, 'x') + "\"}";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  ASSERT_EQ(file.write(content.data(), content.size()), content.size());
  ASSERT_TRUE(file.close());
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Copied);
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Refused);
  for (auto path : {primary, backup}) {
    ASSERT_TRUE(storage.openFileForRead("TEST", path, file));
    std::string read(content.size(), '\0');
    ASSERT_EQ(file.read(read.data(), read.size()), static_cast<int>(read.size()));
    EXPECT_EQ(read, content);
    EXPECT_TRUE(file.close());
  }
}

TEST(SettingsSnapshotCopy, FailedWriteLeavesSourceAndPartialSnapshotInPlace) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  const char content[] = "{\"fontSize\":14}";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  ASSERT_EQ(file.write(content, sizeof(content) - 1), sizeof(content) - 1);
  ASSERT_TRUE(file.close());
  storage.failNextWriteFor(backup, 4);
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Failed);
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Refused);
  ASSERT_TRUE(storage.openFileForRead("TEST", primary, file));
  char after[sizeof(content)]{};
  ASSERT_EQ(file.read(after, sizeof(content) - 1), static_cast<int>(sizeof(content) - 1));
  EXPECT_STREQ(after, content);
  EXPECT_TRUE(file.close());
  EXPECT_TRUE(storage.exists(backup));
}

TEST(SettingsSnapshotCopy, RefusesInvalidInputAndPreservesPartialFileAfterSyncFailure) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  const char invalid[] = "[1,2]";
  ASSERT_EQ(file.write(invalid, sizeof(invalid) - 1), sizeof(invalid) - 1);
  ASSERT_TRUE(file.close());
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Refused);
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, primary), settingsrecovery::CopyResult::Refused);
  EXPECT_FALSE(storage.exists(backup));
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  const char valid[] = "{}";
  ASSERT_EQ(file.write(valid, sizeof(valid) - 1), sizeof(valid) - 1);
  ASSERT_TRUE(file.close());
  storage.failNextNewSyncFor(backup);
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Failed);
  EXPECT_TRUE(storage.exists(backup));
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Refused);
  EXPECT_TRUE(storage.exists(primary));
}

TEST(SettingsRecoveryProbe, RequiresWholeJsonObjectWithOnlyTrailingWhitespace) {
  auto& storage = HalStorage::getInstance();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  const std::pair<std::string, bool> examples[] = {
      {"{\"x\":\"}\\\"{\"} \r\n\t", true}, {"{}garbage", false}, {"{}{}", false},
      {std::string("{}\0", 3), false},     {"{\"x\":", false},   {"[]", false}};
  for (const auto& [content, expectedValid] : examples) {
    storage.reset();
    HalFile file;
    ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
    ASSERT_EQ(file.write(content.data(), content.size()), content.size());
    ASSERT_TRUE(file.close());
    EXPECT_EQ(settingsrecovery::probe(primary, "/missing.bak", "/missing.tmp").primary,
              expectedValid ? Candidate::JsonObject : Candidate::Invalid);
    EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup),
              expectedValid ? settingsrecovery::CopyResult::Copied : settingsrecovery::CopyResult::Refused);
  }
}

TEST(SettingsSnapshotCopy, CheckedHandleFailuresDoNotOverwriteSource) {
  auto& storage = HalStorage::getInstance();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  for (int failureAt = 0; failureAt < 3; ++failureAt) {
    storage.reset();
    HalFile file;
    ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
    const char valid[] = "{\"fontSize\":14}";
    ASSERT_EQ(file.write(valid, sizeof(valid) - 1), sizeof(valid) - 1);
    ASSERT_TRUE(file.close());
    storage.failCheckedOpenAfter(failureAt);
    EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Failed);
    ASSERT_TRUE(storage.openFileForRead("TEST", primary, file));
    char after[sizeof(valid)]{};
    ASSERT_EQ(file.read(after, sizeof(valid) - 1), static_cast<int>(sizeof(valid) - 1));
    EXPECT_STREQ(after, valid);
    EXPECT_TRUE(file.close());
    EXPECT_EQ(storage.exists(backup), failureAt == 2);
  }
}

TEST(SettingsSnapshotCopy, UnverifiableAllocationDoesNotReportACopy) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  const char content[] = "{\"fontSize\":14}";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  ASSERT_EQ(file.write(content, sizeof(content) - 1), sizeof(content) - 1);
  ASSERT_TRUE(file.close());
  storage.failSettingsIdentity();
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Failed);
  ASSERT_TRUE(storage.openFileForRead("TEST", primary, file));
  char after[sizeof(content)]{};
  ASSERT_EQ(file.read(after, sizeof(content) - 1), static_cast<int>(sizeof(content) - 1));
  EXPECT_STREQ(after, content);
  EXPECT_TRUE(file.close());
  EXPECT_TRUE(storage.exists(backup));
}

TEST(SettingsSnapshotCopy, IneligibleSourceDoesNotCreateDestination) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  constexpr auto backup = "/.crosspoint/settings.snapshot";
  const char content[] = "{\"fontSize\":14}";
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  ASSERT_EQ(file.write(content, sizeof(content) - 1), sizeof(content) - 1);
  ASSERT_TRUE(file.close());
  storage.failSettingsEligibility();
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, backup), settingsrecovery::CopyResult::Failed);
  EXPECT_FALSE(storage.exists(backup));
  ASSERT_TRUE(storage.openFileForRead("TEST", primary, file));
  char after[sizeof(content)]{};
  ASSERT_EQ(file.read(after, sizeof(content) - 1), static_cast<int>(sizeof(content) - 1));
  EXPECT_STREQ(after, content);
  EXPECT_TRUE(file.close());
}

TEST(SettingsJsonAllocator, RefusesGrowthBeyondBudgetAndPreservesOriginalAllocation) {
  settingsrecovery::SettingsJsonAllocator allocator;
  auto* first = static_cast<char*>(allocator.allocate(100));
  ASSERT_NE(first, nullptr);
  first[0] = 'x';
  const auto used = allocator.bytesInUse();
  EXPECT_EQ(allocator.reallocate(first, settingsrecovery::SettingsJsonAllocator::LIMIT), nullptr);
  EXPECT_EQ(first[0], 'x');
  EXPECT_EQ(allocator.bytesInUse(), used);
  auto* grown = static_cast<char*>(allocator.reallocate(first, 200));
  ASSERT_NE(grown, nullptr);
  EXPECT_EQ(grown[0], 'x');
  EXPECT_LE(allocator.peakBytes(), settingsrecovery::SettingsJsonAllocator::LIMIT);
  allocator.deallocate(grown);
  EXPECT_EQ(allocator.bytesInUse(), 0u);
}

TEST(SettingsJsonAllocator, CountsBothAllocationsDuringGrowth) {
  settingsrecovery::SettingsJsonAllocator allocator;
  auto* original = static_cast<char*>(allocator.allocate(9000));
  ASSERT_NE(original, nullptr);
  original[0] = 'x';
  // Each allocation fits by itself, but both cannot coexist within the cap.
  EXPECT_EQ(allocator.reallocate(original, 8000), nullptr);
  EXPECT_EQ(original[0], 'x');
  EXPECT_LE(allocator.peakBytes(), settingsrecovery::SettingsJsonAllocator::LIMIT);
  allocator.deallocate(original);
  EXPECT_EQ(allocator.bytesInUse(), 0u);
}

TEST(SettingsRecoveryPlan, SyntacticObjectRequiresInspectionBeforeUse) {
  using settingsrecovery::Candidate;
  using settingsrecovery::Plan;
  // An object alone provides no evidence that it is a compatible settings file.
  EXPECT_EQ(settingsrecovery::plan(Candidate::JsonObject, Candidate::Missing, Candidate::Missing),
            Plan::InspectPrimary);
  EXPECT_EQ(settingsrecovery::plan(Candidate::Missing, Candidate::JsonObject, Candidate::Missing), Plan::InspectBackup);
}

TEST(SettingsRecoveryProbe, ExcessiveJsonTreeReturnsUnavailableWithoutWriting) {
  auto& storage = HalStorage::getInstance();
  storage.reset();
  constexpr auto primary = "/.crosspoint/settings.json";
  // Build a syntactically valid object with many scalar entries.
  std::string entries = "{\"entries\":[";
  for (int i = 0; i < 9000; ++i) entries += i ? ",0" : "0";
  entries += "]}";
  ASSERT_LT(entries.size(), 32768u);
  HalFile file;
  ASSERT_TRUE(storage.openFileForWrite("TEST", primary, file));
  ASSERT_EQ(file.write(entries.data(), entries.size()), entries.size());
  ASSERT_TRUE(file.close());
  EXPECT_EQ(settingsrecovery::probe(primary, "/missing.bak", "/missing.tmp").primary, Candidate::Unavailable);
  EXPECT_EQ(settingsrecovery::copySnapshot(primary, "/.crosspoint/settings.snapshot"),
            settingsrecovery::CopyResult::Failed);
  EXPECT_FALSE(storage.exists("/.crosspoint/settings.snapshot"));
}
