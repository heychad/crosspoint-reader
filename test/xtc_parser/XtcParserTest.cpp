// Adapted from CrossPoint PR #3806 by Daniil Pankov (T-Damer).
// Synthetic regression cases originated in lpla's PR review.
// https://github.com/crosspoint-reader/crosspoint-reader/pull/3806

#include <HalStorage.h>
#include <XtcParser.h>
#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <vector>

using namespace xtc;

namespace {

constexpr const char* kPath = "/book.xtc";
constexpr uint16_t kSide = 8;
constexpr uint8_t kFill = 0x55;

// 1-bit pages are row-major ((w+7)/8 * h); 2-bit pages are two column-major planes (w * ceil(h/8) * 2).
uint32_t payloadSize(uint32_t pageMagic) { return pageMagic == XTH_MAGIC ? kSide * ((kSide + 7) / 8) * 2 : 8; }

std::vector<uint8_t> buildBook(uint32_t containerMagic, uint32_t pageMagic, uint16_t pageCount = 1) {
  XtcHeader header{};
  header.magic = containerMagic;
  header.versionMajor = 1;
  header.pageCount = pageCount;
  header.pageTableOffset = sizeof(XtcHeader);
  header.dataOffset = sizeof(XtcHeader) + pageCount * sizeof(PageTableEntry);

  XtgPageHeader page{};
  page.magic = pageMagic;
  page.width = kSide;
  page.height = kSide;
  page.dataSize = payloadSize(pageMagic);

  const auto pageSize = static_cast<uint32_t>(sizeof(XtgPageHeader) + page.dataSize);
  std::vector<uint8_t> bytes(header.dataOffset + pageCount * pageSize, kFill);
  std::memcpy(bytes.data(), &header, sizeof(header));
  for (uint16_t i = 0; i < pageCount; ++i) {
    const PageTableEntry entry{header.dataOffset + i * pageSize, pageSize, kSide, kSide};
    std::memcpy(bytes.data() + header.pageTableOffset + i * sizeof(entry), &entry, sizeof(entry));
    std::memcpy(bytes.data() + entry.dataOffset, &page, sizeof(page));
  }
  return bytes;
}

void setFirstPageOffset(std::vector<uint8_t>& bytes, uint64_t offset) {
  PageTableEntry entry;
  std::memcpy(&entry, bytes.data() + sizeof(XtcHeader), sizeof(entry));
  entry.dataOffset = offset;
  std::memcpy(bytes.data() + sizeof(XtcHeader), &entry, sizeof(entry));
}

struct DepthCase {
  const char* name;
  uint32_t containerMagic;
  uint32_t pageMagic;
  uint8_t expectedDepth;
};

class XtcParserDepth : public ::testing::TestWithParam<DepthCase> {};

// Matching files keep working, and mismatched ones (xtctool writes "XTC\0" around
// "XTH\0" pages) follow the bit depth of their pages.
TEST_P(XtcParserDepth, BitDepthFollowsPagesAndPageLoads) {
  const DepthCase& c = GetParam();
  Storage.setFile(kPath, buildBook(c.containerMagic, c.pageMagic));

  XtcParser parser;
  ASSERT_EQ(parser.open(kPath), XtcError::OK);
  EXPECT_EQ(parser.getBitDepth(), c.expectedDepth);

  PageInfo info{};
  ASSERT_TRUE(parser.getPageInfo(0, info));
  EXPECT_EQ(info.bitDepth, c.expectedDepth);

  const size_t expected = payloadSize(c.pageMagic);
  uint8_t buffer[64]{};
  ASSERT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), expected);
  for (size_t i = 0; i < expected; i++) EXPECT_EQ(buffer[i], kFill) << "byte " << i;

  EXPECT_EQ(parser.loadPage(0, buffer, expected - 1), 0u);
  EXPECT_EQ(parser.getLastError(), XtcError::MEMORY_ERROR);

  size_t streamed = 0;
  const XtcError status = parser.loadPageStreaming(
      0,
      [&](const uint8_t* data, size_t size, size_t offset) {
        EXPECT_EQ(offset, streamed);
        for (size_t i = 0; i < size; i++) EXPECT_EQ(data[i], kFill);
        streamed += size;
      },
      3);
  EXPECT_EQ(status, XtcError::OK);
  EXPECT_EQ(streamed, expected);
}

INSTANTIATE_TEST_SUITE_P(Containers, XtcParserDepth,
                         ::testing::Values(DepthCase{"XtcWithXtgPages", XTC_MAGIC, XTG_MAGIC, 1},
                                           DepthCase{"XtchWithXthPages", XTCH_MAGIC, XTH_MAGIC, 2},
                                           DepthCase{"XtcWithXthPages", XTC_MAGIC, XTH_MAGIC, 2},
                                           DepthCase{"XtchWithXtgPages", XTCH_MAGIC, XTG_MAGIC, 1}),
                         [](const ::testing::TestParamInfo<DepthCase>& info) { return info.param.name; });

// An unknown first-page magic does not override the container; the page is still rejected.
TEST(XtcParser, UnknownPageMagicKeepsContainerDepth) {
  for (const uint32_t container : {XTC_MAGIC, XTCH_MAGIC}) {
    Storage.setFile(kPath, buildBook(container, 0x12345678));
    XtcParser parser;
    ASSERT_EQ(parser.open(kPath), XtcError::OK);
    EXPECT_EQ(parser.getBitDepth(), container == XTCH_MAGIC ? 2 : 1);
    uint8_t buffer[64]{};
    EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
    EXPECT_EQ(parser.getLastError(), XtcError::INVALID_MAGIC);
  }
}

// A first-page offset beyond the file (including one that would overflow offset + header size)
// is ignored by the probe: open still succeeds with the container depth, the page fails.
TEST(XtcParser, OutOfRangeFirstPageOffsetKeepsContainerDepth) {
  for (const uint64_t offset : {uint64_t{1} << 40, std::numeric_limits<uint64_t>::max() - 1}) {
    auto bytes = buildBook(XTC_MAGIC, XTH_MAGIC);
    setFirstPageOffset(bytes, offset);
    Storage.setFile(kPath, std::move(bytes));
    XtcParser parser;
    ASSERT_EQ(parser.open(kPath), XtcError::OK);
    EXPECT_EQ(parser.getBitDepth(), 1);
    uint8_t buffer[64]{};
    EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
  }
}

// A first page cut off inside its magic is ignored the same way.
TEST(XtcParser, TruncatedFirstPageKeepsContainerDepth) {
  auto bytes = buildBook(XTC_MAGIC, XTH_MAGIC);
  bytes.resize(sizeof(XtcHeader) + sizeof(PageTableEntry) + 2);
  Storage.setFile(kPath, std::move(bytes));
  XtcParser parser;
  ASSERT_EQ(parser.open(kPath), XtcError::OK);
  EXPECT_EQ(parser.getBitDepth(), 1);
  uint8_t buffer[64]{};
  EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
}

TEST(XtcParser, CorruptedOppositeFirstPageMagicKeepsLaterPageReadable) {
  auto bytes = buildBook(XTC_MAGIC, XTG_MAGIC, 2);
  const size_t firstPage = sizeof(XtcHeader) + 2 * sizeof(PageTableEntry);
  const uint32_t wrongMagic = XTH_MAGIC;
  std::memcpy(bytes.data() + firstPage, &wrongMagic, sizeof(wrongMagic));
  Storage.setFile(kPath, std::move(bytes));
  XtcParser parser;
  ASSERT_EQ(parser.open(kPath), XtcError::OK);
  EXPECT_EQ(parser.getBitDepth(), 1);
  uint8_t buffer[64]{};
  EXPECT_EQ(parser.loadPage(0, buffer, sizeof(buffer)), 0u);
  EXPECT_EQ(parser.loadPage(1, buffer, sizeof(buffer)), payloadSize(XTG_MAGIC));
  size_t streamed = 0;
  EXPECT_EQ(
      parser.loadPageStreaming(1, [&](const uint8_t*, size_t size, size_t) { streamed += size; }, 3), XtcError::OK);
  EXPECT_EQ(streamed, payloadSize(XTG_MAGIC));
}

TEST(XtcParser, OppositePageWithPayloadOutsideItsEntryKeepsContainerDepth) {
  auto bytes = buildBook(XTC_MAGIC, XTH_MAGIC);
  PageTableEntry entry;
  std::memcpy(&entry, bytes.data() + sizeof(XtcHeader), sizeof(entry));
  entry.dataSize = sizeof(XtgPageHeader);
  std::memcpy(bytes.data() + sizeof(XtcHeader), &entry, sizeof(entry));
  Storage.setFile(kPath, std::move(bytes));
  XtcParser parser;
  ASSERT_EQ(parser.open(kPath), XtcError::OK);
  EXPECT_EQ(parser.getBitDepth(), 1);
}

// Failed access must not change the container-derived depth or fail opening.
TEST(XtcParser, ProbeIoFailuresKeepContainerDepth) {
  for (bool failSeek : {false, true}) {
    Storage.setFile(kPath, buildBook(XTC_MAGIC, XTH_MAGIC));
    HalFile::failedSeekOffset = failSeek ? sizeof(XtcHeader) + sizeof(PageTableEntry) : UINT64_MAX;
    HalFile::shortReadOffset = failSeek ? UINT64_MAX : sizeof(XtcHeader) + sizeof(PageTableEntry);
    XtcParser parser;
    EXPECT_EQ(parser.open(kPath), XtcError::OK);
    EXPECT_EQ(parser.getBitDepth(), 1);
    HalFile::failedSeekOffset = HalFile::shortReadOffset = UINT64_MAX;
  }
}

}  // namespace
