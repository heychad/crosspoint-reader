#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "LibrarySort.h"

namespace {

constexpr char RUN_PATH[] = "/.crosspoint/test-sort.run";

struct SortFixtureData {
  std::vector<std::string> keys;
  std::vector<uint16_t> output;
  bool failLoad = false;
  bool failEmit = false;
};

bool loadSegment(void* pointer, const uint16_t source, const size_t offset, char* segment, size_t& valueBytes) {
  auto& data = *static_cast<SortFixtureData*>(pointer);
  if (data.failLoad) return false;
  const std::string& value = data.keys.at(source);
  memset(segment, value.empty() ? 0xFF : 0, library::SORT_SEGMENT_BYTES);
  valueBytes = value.size();
  if (offset < value.size()) {
    memcpy(segment, value.data() + offset, std::min(value.size() - offset, library::SORT_SEGMENT_BYTES));
  }
  return true;
}

bool emit(void* pointer, const library::SortEntry& entry) {
  auto& data = *static_cast<SortFixtureData*>(pointer);
  if (data.failEmit) return false;
  data.output.push_back(entry.ordinal);
  return true;
}

class LibraryMetadataSort : public ::testing::Test {
 protected:
  SortFixtureData data;

  void SetUp() override {
    fake::reset();
    library::setSortRunCapacityForTesting(16);
    for (unsigned i = 0; i < 64; ++i) {
      data.keys.push_back(std::string(500, 'a') + char('z' - i % 26));
    }
  }

  void TearDown() override { library::setSortRunCapacityForTesting(0); }

  library::SortConfig config() {
    library::SortConfig result;
    result.runPath = RUN_PATH;
    result.keyBytes = 512;
    result.load = &loadSegment;
    result.loadContext = &data;
    return result;
  }

  bool add(library::ExternalSorter& sorter) {
    for (uint16_t i = 0; i < data.keys.size(); ++i) {
      library::SortEntry entry{};
      entry.ordinal = i;
      entry.source = i;
      size_t length = 0;
      if (!loadSegment(&data, i, 0, entry.key, length) || !sorter.add(entry)) return false;
    }
    return true;
  }
};

}  // namespace

TEST_F(LibraryMetadataSort, RefinesLongSharedPrefixesAcrossSpilledRuns) {
  data.keys[7].clear();
  library::ExternalSorter sorter;
  ASSERT_TRUE(sorter.begin(config(), data.keys.size()));
  ASSERT_TRUE(add(sorter));
  ASSERT_TRUE(sorter.spilled());
  ASSERT_TRUE(sorter.finish(&emit, &data));
  std::vector<uint16_t> expected(data.keys.size());
  std::iota(expected.begin(), expected.end(), 0);
  std::stable_sort(expected.begin(), expected.end(), [&](const uint16_t a, const uint16_t b) {
    if (data.keys[a].empty() != data.keys[b].empty()) return !data.keys[a].empty();
    return data.keys[a] < data.keys[b];
  });
  EXPECT_EQ(data.output, expected);
  EXPECT_FALSE(Storage.exists(RUN_PATH));
}

TEST_F(LibraryMetadataSort, FailingRefinementStopsAndRemovesRuns) {
  library::ExternalSorter sorter;
  ASSERT_TRUE(sorter.begin(config(), data.keys.size()));
  ASSERT_TRUE(add(sorter));
  data.failLoad = true;
  EXPECT_FALSE(sorter.finish(&emit, &data));
  EXPECT_FALSE(Storage.exists(RUN_PATH));
}

TEST_F(LibraryMetadataSort, FailingEmitterStopsAndRemovesRuns) {
  library::ExternalSorter sorter;
  ASSERT_TRUE(sorter.begin(config(), data.keys.size()));
  ASSERT_TRUE(add(sorter));
  data.failEmit = true;
  EXPECT_FALSE(sorter.finish(&emit, &data));
  EXPECT_FALSE(Storage.exists(RUN_PATH));
}

TEST_F(LibraryMetadataSort, ShortRunWriteFailsWithoutPublishingOutput) {
  {
    library::ExternalSorter sorter;
    ASSERT_TRUE(sorter.begin(config(), data.keys.size()));
    fake::shortWrite = 1;
    EXPECT_FALSE(add(sorter));
  }
  EXPECT_TRUE(data.output.empty());
  EXPECT_FALSE(Storage.exists(RUN_PATH));
}

TEST_F(LibraryMetadataSort, ClearsOldScratchEvenWhenTheNewSortFitsInMemory) {
  fake::add(RUN_PATH, "incomplete previous run");
  data.keys.resize(2);
  library::ExternalSorter sorter;
  ASSERT_TRUE(sorter.begin(config(), data.keys.size()));
  ASSERT_TRUE(add(sorter));
  ASSERT_TRUE(sorter.finish(&emit, &data));
  EXPECT_EQ(data.output.size(), 2u);
  EXPECT_FALSE(Storage.exists(RUN_PATH));
}
