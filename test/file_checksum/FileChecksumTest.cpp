#include <Memory.h>
#include <gtest/gtest.h>
#include <mbedtls/sha256.h>

#include "network/FileChecksum.h"

namespace {
using filechecksum::Job;
using filechecksum::State;
constexpr char PATH[] = "/Authors/Café.epub";
class FileChecksumTest : public ::testing::Test {
 protected:
  HalStorage storage;
  void SetUp() override {
    fake::files.clear();
    fake::epoch = fake::writers = 0;
    fake::failRead = fake::failClose = failAllocation = false;
    fakehash::failStart = fakehash::failUpdate = fakehash::failFinish = false;
    auto file = std::make_shared<fake::File>();
    file->bytes = {'a', 'b', 'c'};
    fake::files[PATH] = file;
  }
};

TEST_F(FileChecksumTest, HashesSavedBytesWithoutWritingOrRetainingAFile) {
  Job job(storage, PATH, 3);
  ASSERT_EQ(job.state(), State::Pending);
  EXPECT_EQ(fake::files.at(PATH).use_count(), 1);
  EXPECT_EQ(job.digest(), nullptr);
  job.advance();
  EXPECT_EQ(job.state(), State::Complete);
  EXPECT_STREQ(job.digest(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  EXPECT_EQ(job.processed(), 3);
  EXPECT_EQ(fake::files.at(PATH).use_count(), 1);
  EXPECT_EQ(fake::epoch, 0);
  EXPECT_EQ(fake::files.at(PATH)->bytes, (std::vector<uint8_t>{'a', 'b', 'c'}));
}

TEST_F(FileChecksumTest, HashesEmptyFiles) {
  fake::files.at(PATH)->bytes.clear();
  Job job(storage, PATH, 0);
  job.advance();
  EXPECT_EQ(job.state(), State::Complete);
  EXPECT_STREQ(job.digest(), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(FileChecksumTest, SlicesLargeFilesAndReopensAtTheSavedOffset) {
  const size_t bytes = filechecksum::SLICE_BYTES * 2 + 5;
  fake::files.at(PATH)->bytes.assign(bytes, 'a');
  Job job(storage, PATH, bytes);
  for (unsigned i = 1; i <= 3; ++i) {
    job.advance();
    EXPECT_EQ(job.processed(), std::min<size_t>(bytes, i * filechecksum::SLICE_BYTES));
    EXPECT_EQ(fake::files.at(PATH).use_count(), 1);
    EXPECT_EQ(job.state(), i == 3 ? State::Complete : State::Pending);
  }
  EXPECT_STREQ(job.digest(), "a23c510dda1fada52784fa215c594d7428c2ebc447630908887eee6e19d948e1");
}

TEST_F(FileChecksumTest, CancelStopsWorkWithoutDeletingTheFile) {
  fake::files.at(PATH)->bytes.resize(filechecksum::SLICE_BYTES + 1);
  Job job(storage, PATH, fake::files.at(PATH)->bytes.size());
  job.advance();
  job.cancel();
  job.advance();
  EXPECT_EQ(job.state(), State::Cancelled);
  EXPECT_EQ(job.digest(), nullptr);
  EXPECT_EQ(job.processed(), filechecksum::SLICE_BYTES);
  EXPECT_TRUE(fake::files.count(PATH));
  EXPECT_EQ(fake::files.at(PATH).use_count(), 1);
}

TEST_F(FileChecksumTest, RefusesMissingWrongSizedDirectoryAndBusyStorage) {
  EXPECT_EQ(Job(storage, "/missing.epub", 3).state(), State::NotFound);
  EXPECT_EQ(Job(storage, PATH, 4).state(), State::WrongSize);
  EXPECT_EQ(Job(storage, "/Authors", 0).state(), State::IoError);
  fake::writers = 1;
  EXPECT_EQ(Job(storage, PATH, 3).state(), State::Unavailable);
}

TEST_F(FileChecksumTest, InvalidatesSameSizeEditsAndTruncationBetweenSlices) {
  fake::files.at(PATH)->bytes.resize(filechecksum::SLICE_BYTES + 1);
  {
    Job job(storage, PATH, fake::files.at(PATH)->bytes.size());
    job.advance();
    fake::files.at(PATH)->bytes[0] ^= 1;
    ++fake::epoch;
    job.advance();
    EXPECT_EQ(job.state(), State::Changed);
    EXPECT_EQ(job.digest(), nullptr);
  }
  {
    Job job(storage, PATH, fake::files.at(PATH)->bytes.size());
    job.advance();
    fake::files.at(PATH)->bytes.resize(1);
    job.advance();
    EXPECT_EQ(job.state(), State::WrongSize);
    EXPECT_EQ(job.digest(), nullptr);
    EXPECT_EQ(fake::files.at(PATH).use_count(), 1);
  }
}

TEST_F(FileChecksumTest, InvalidatesACompletedResultAfterAnotherMutation) {
  Job job(storage, PATH, 3);
  job.advance();
  ASSERT_EQ(job.state(), State::Complete);
  ++fake::epoch;
  EXPECT_EQ(job.state(), State::Changed);
  EXPECT_EQ(job.digest(), nullptr);
}

TEST_F(FileChecksumTest, ReportsReadAndCloseFailuresWithoutADigest) {
  {
    Job job(storage, PATH, 3);
    fake::failRead = true;
    job.advance();
    EXPECT_EQ(job.state(), State::IoError);
    EXPECT_EQ(job.digest(), nullptr);
    EXPECT_EQ(fake::files.at(PATH).use_count(), 1);
  }
  fake::failRead = false;
  {
    Job job(storage, PATH, 3);
    fake::failClose = true;
    job.advance();
    EXPECT_EQ(job.state(), State::IoError);
    EXPECT_EQ(job.digest(), nullptr);
  }
  EXPECT_EQ(Job(storage, PATH, 3).state(), State::IoError);
}

TEST_F(FileChecksumTest, HandlesAllocationAndHashFailures) {
  failAllocation = true;
  EXPECT_EQ(Job(storage, PATH, 3).state(), State::NoMemory);
  failAllocation = false;
  fakehash::failStart = true;
  EXPECT_EQ(Job(storage, PATH, 3).state(), State::HashError);
  fakehash::failStart = false;
  {
    Job job(storage, PATH, 3);
    fakehash::failUpdate = true;
    job.advance();
    EXPECT_EQ(job.state(), State::HashError);
    EXPECT_EQ(job.digest(), nullptr);
  }
  fakehash::failUpdate = false;
  {
    Job job(storage, PATH, 3);
    fakehash::failFinish = true;
    job.advance();
    EXPECT_EQ(job.state(), State::HashError);
    EXPECT_EQ(job.digest(), nullptr);
  }
}

TEST_F(FileChecksumTest, RestrictsCanonicalVisiblePathsIncludingFatAliases) {
  EXPECT_TRUE(filechecksum::validPath(PATH));
  EXPECT_TRUE(filechecksum::validPath("/Books/cover.bmp"));
  for (const char* path : {"", "/", "book.epub", "/Books//book.epub", "/Books/../book.epub", "/Books/./book.epub",
                           "/.crosspoint/wifi.json", "/CROSSP~1/WIFI.JSO", "/Books/ SECRET~1.EPU",
                           "/Books/SECRET~1.EPU", "/Books/.hidden/book.epub", "/Books/book.epub.", "/Books/book.epub ",
                           "/XTCache/book.epub", "/System Volume Information/a", "/Books/"})
    EXPECT_FALSE(filechecksum::validPath(path)) << path;
  EXPECT_FALSE(filechecksum::validPath(std::string_view("/Books/a\0.epub", 14)));
  EXPECT_FALSE(filechecksum::validPath("/Books/\xff.epub"));
  EXPECT_FALSE(filechecksum::validPath("/" + std::string(filechecksum::MAX_PATH_BYTES, 'a')));
}

TEST_F(FileChecksumTest, ParsesOnlyBoundedDecimalSizesWithoutChangingRejectedOutput) {
  uint64_t size = 99;
  for (const char* text : {"", "00", "03", "-1", "+3", " 3", "3x", "4294967296", "9999999999999999999999"}) {
    EXPECT_FALSE(filechecksum::parseSize(text, size));
    EXPECT_EQ(size, 99);
  }
  EXPECT_TRUE(filechecksum::parseSize("0", size));
  EXPECT_EQ(size, 0);
  EXPECT_TRUE(filechecksum::parseSize("4294967295", size));
  EXPECT_EQ(size, filechecksum::MAX_FILE_BYTES);
}
}  // namespace
