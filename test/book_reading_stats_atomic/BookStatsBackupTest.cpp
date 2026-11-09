#include <Arduino.h>
#include <BookReadingStats.h>
#include <BookStatsBackup.h>
#include <HalStorage.h>
#include <ReadingStatsStore.h>
#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

namespace {
constexpr char CACHE[] = "/.crosspoint/epub_123";
constexpr char BACKUPS[] = "/.crossink-stats-backup/books/epub_123";
BookReadingStats fixture() {
  BookReadingStats book;
  book.totalReadingSeconds = 1234;
  book.sessionCount = 3;
  book.startDate = {2024, 2, 29};
  book.startDateManual = true;
  return book;
}
std::vector<std::string> names() {
  std::vector<std::string> result;
  auto directory = Storage.open(BACKUPS);
  if (!directory) return result;
  for (auto entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
    char name[128]{};
    entry.getName(name, sizeof(name));
    result.emplace_back(name);
  }
  return result;
}
class BookStatsBackupTest : public testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    testMillis = 7;
    ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  }
};
}  // namespace

TEST_F(BookStatsBackupTest, CreatesExactVerifiedBookSnapshotWithoutOverwrite) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  auto files = names();
  ASSERT_EQ(files.size(), 1U);
  EXPECT_TRUE(files[0].starts_with("book_0000000008_"));
  HalFile file;
  ASSERT_TRUE(Storage.openFileForRead("TEST", std::string(BACKUPS) + "/" + files[0], file));
  std::array<uint8_t, BookReadingStats::CURRENT_FILE_SIZE> actual{}, expected{};
  ASSERT_TRUE(fixture().encode(expected.data(), expected.size()));
  ASSERT_EQ(file.read(actual.data(), actual.size()), static_cast<int>(actual.size()));
  EXPECT_EQ(actual, expected);
  EXPECT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, RejectsMissingUnreadableOrChangedSource) {
  auto changed = fixture();
  ++changed.totalReadingSeconds;
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, changed));
  const auto path = readingstats::bookDirectory(CACHE) + "/stats_v5.bin";
  Storage.failReadsFor(path);
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  Storage.reset();
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(names().empty());
}

TEST_F(BookStatsBackupTest, RejectsUnsafeIdentityAndSourceCloseFailure) {
  EXPECT_FALSE(statsbackup::createBookBackup("/.crosspoint/..", fixture()));
  EXPECT_FALSE(statsbackup::createBookBackup("/.crosspoint/", fixture()));
  Storage.failNextCloseFor(readingstats::bookDirectory(CACHE) + "/stats_v5.bin");
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(names().empty());
}

TEST_F(BookStatsBackupTest, FailedWritePreservesIncompleteTargetAndSource) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  const std::string target = std::string(BACKUPS) + "/" + name;
  Storage.failNextWriteFor(target, 10);
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  HalFile incomplete;
  ASSERT_TRUE(Storage.openFileForRead("TEST", target, incomplete));
  EXPECT_EQ(incomplete.fileSize(), 10U);
  EXPECT_TRUE(incomplete.close());
  EXPECT_TRUE(Storage.exists((readingstats::bookDirectory(CACHE) + "/stats_v5.bin").c_str()));
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  ASSERT_TRUE(Storage.openFileForRead("TEST", target, incomplete));
  EXPECT_EQ(incomplete.fileSize(), 10U);
  EXPECT_TRUE(incomplete.close());
  EXPECT_EQ(names().size(), 2U);
}

TEST_F(BookStatsBackupTest, ExistingLegacyPartialIsPreservedAndBlocksSameName) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  const std::string target = std::string(BACKUPS) + "/" + name;
  const std::string partial = target + ".part";
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", partial, file));
  ASSERT_EQ(file.write(uint8_t{42}), 1U);
  ASSERT_TRUE(file.close());
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(Storage.exists(partial.c_str()));
  EXPECT_FALSE(Storage.exists(target.c_str()));
}

TEST_F(BookStatsBackupTest, FailedTargetClosePreservesFileAndPreventsOverwrite) {
  ASSERT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  const auto name = names().at(0);
  Storage.reset();
  ASSERT_TRUE(readingstats::saveBook(readingstats::bookDirectory(CACHE), fixture()));
  const std::string target = std::string(BACKUPS) + "/" + name;
  Storage.failNextNewCloseFor(target);
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_TRUE(Storage.exists(target.c_str()));
  EXPECT_FALSE(statsbackup::createBookBackup(CACHE, fixture()));
  ++testMillis;
  EXPECT_TRUE(statsbackup::createBookBackup(CACHE, fixture()));
  EXPECT_EQ(names().size(), 2U);
}
