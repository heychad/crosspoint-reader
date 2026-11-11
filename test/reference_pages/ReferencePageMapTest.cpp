#include <Memory.h>
#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

#include "Epub/ReferencePageMap.h"

namespace {
std::string document(const std::string& spine, const std::string& extra = "") {
  return "{\"format\":\"x-locations\",\"version\":1,\"totalLocations\":20,"
         "\"totalWords\":200,\"wordsPerReferencePage\":50," +
         extra + "\"spine\":[" + spine + "]}";
}
const std::string ranges = "{\"wordStart\":0,\"wordCount\":100},{\"wordStart\":100,\"wordCount\":100}";
bool parse(ReferencePageMap& map, const std::string& json, size_t count = 2, size_t chunk = 7) {
  ReferencePageParser parser(map, count);
  if (!parser.ready()) return false;
  for (size_t offset = 0; offset < json.size(); offset += chunk) {
    const auto size = std::min(chunk, json.size() - offset);
    if (parser.write(reinterpret_cast<const uint8_t*>(json.data() + offset), size) != size) return false;
  }
  return parser.finish();
}
}  // namespace

TEST(ReferencePageMap, WordRangesAndBoundaries) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document(ranges)));
  uint32_t page = 0, total = 0;
  ASSERT_TRUE(map.resolve(0, 0, page, total));
  EXPECT_EQ(page, 1u);
  EXPECT_EQ(total, 4u);
  ASSERT_TRUE(map.resolve(0, .5f, page, total));
  EXPECT_EQ(page, 2u);
  ASSERT_TRUE(map.resolve(1, 0, page, total));
  EXPECT_EQ(page, 3u);
  ASSERT_TRUE(map.resolve(1, 1, page, total));
  EXPECT_EQ(page, 4u);
  ASSERT_TRUE(map.resolve(1, 9, page, total));
  EXPECT_EQ(page, 4u);
}

TEST(ReferencePageMap, CharacterMetadataTakesPrecedenceRegardlessOfFieldOrder) {
  ReferencePageMap map;
  const std::string json =
      "{\"spine\":[{\"characterStart\":0,\"characterCount\":3000},"
      "{\"characterStart\":3000,\"characterCount\":3000}],"
      "\"totalCharacters\":6000,\"charactersPerReferencePage\":1500,"
      "\"format\":\"crossink-locations\",\"version\":1,\"totalLocations\":200}";
  ASSERT_TRUE(parse(map, json, 2, 1));
  uint32_t page, total;
  ASSERT_TRUE(map.resolve(1, .5f, page, total));
  EXPECT_EQ(page, 4u);
  EXPECT_EQ(total, 4u);
}

TEST(ReferencePageMap, SupportsExplicitIndicesAndEmptySpineRanges) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map,
                    document("{\"index\":2,\"wordStart\":100,\"wordCount\":100},"
                             "{\"index\":0,\"wordStart\":0,\"wordCount\":100}"),
                    3));
  uint32_t page = 99, total = 99;
  EXPECT_FALSE(map.resolve(1, .5f, page, total));
  EXPECT_EQ(page, 0u);
  EXPECT_EQ(total, 0u);
  EXPECT_TRUE(map.resolve(2, 0, page, total));
  EXPECT_EQ(page, 3u);
}

TEST(ReferencePageMap, IgnoresUnknownNestedMetadata) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document(ranges, "\"extra\":{\"spine\":[{\"index\":9999}],\"totalWords\":1},")));
  EXPECT_EQ(map.pageCount(), 4u);
}

TEST(ReferencePageMap, RejectsIncompleteOrWrongStructure) {
  ReferencePageMap map;
  auto json = document(ranges);
  EXPECT_FALSE(parse(map, json.substr(0, json.size() - 1)));
  EXPECT_FALSE(parse(map, "[" + json + "]"));
  EXPECT_FALSE(parse(map, document("1")));
  EXPECT_FALSE(parse(map, document("{}]")));
  EXPECT_FALSE(parse(map, document(ranges) + "{}"));
}

TEST(ReferencePageMap, RejectsMissingOrInconsistentMetadata) {
  ReferencePageMap map;
  EXPECT_FALSE(parse(map, "{\"spine\":[{}]}"));
  EXPECT_FALSE(parse(map, document(ranges, "\"totalReferencePages\":5,")));
  EXPECT_TRUE(parse(map, document(ranges, "\"totalReferencePages\":4,")));
  EXPECT_FALSE(parse(map, document(ranges, "\"version\":2,")));
  EXPECT_FALSE(parse(map, document(ranges, "\"format\":\"other\",")));
}

TEST(ReferencePageMap, RejectsBadNumericTypesAndOverflow) {
  ReferencePageMap map;
  for (const char* bad : {"-1", "1.5", "1e2", "4294967296", "true", "null", "\"50\"", "[]", "{}"}) {
    auto json = document(ranges);
    const auto where = json.find("Page\":50");
    json.replace(where + 6, 2, bad);
    EXPECT_FALSE(parse(map, json)) << bad;
  }
}

TEST(ReferencePageMap, RejectsDuplicateIndicesAndFields) {
  ReferencePageMap map;
  EXPECT_FALSE(parse(map, document("{\"index\":0,\"wordCount\":100},"
                                   "{\"index\":0,\"wordStart\":100,\"wordCount\":100}")));
  EXPECT_FALSE(parse(map, document("{\"wordCount\":100,\"wordCount\":100}")));
  EXPECT_FALSE(parse(map, document(ranges, "\"spine\":[],")));
}

TEST(ReferencePageMap, RejectsInvalidRanges) {
  ReferencePageMap map;
  EXPECT_FALSE(parse(map, document("{\"wordStart\":0,\"wordCount\":101},"
                                   "{\"wordStart\":100,\"wordCount\":100}")));
  EXPECT_FALSE(parse(map, document("{\"wordStart\":200,\"wordCount\":1}")));
  EXPECT_FALSE(parse(map, document("{\"wordStart\":100,\"wordCount\":101}")));
  EXPECT_FALSE(parse(map, document("{}")));
}

TEST(ReferencePageMap, HandlesUint32MaximumWithoutArithmeticOverflow) {
  ReferencePageMap map;
  const std::string json =
      "{\"format\":\"x-locations\",\"version\":1,\"totalLocations\":1,"
      "\"totalWords\":4294967295,\"wordsPerReferencePage\":1,"
      "\"spine\":[{\"wordCount\":4294967295}]}";
  ASSERT_TRUE(parse(map, json, 1));
  uint32_t page, total;
  ASSERT_TRUE(map.resolve(0, 1, page, total));
  EXPECT_EQ(page, UINT32_MAX);
  EXPECT_EQ(total, UINT32_MAX);
}

TEST(ReferencePageMap, RejectsInvalidReadPositions) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document(ranges)));
  uint32_t page, total;
  EXPECT_FALSE(map.resolve(-1, 0, page, total));
  EXPECT_FALSE(map.resolve(2, 0, page, total));
  EXPECT_FALSE(map.resolve(0, std::numeric_limits<float>::quiet_NaN(), page, total));
  EXPECT_FALSE(map.resolve(0, std::numeric_limits<float>::infinity(), page, total));
}

TEST(ReferencePageMap, BoundsInputAndSpineAllocation) {
  ReferencePageMap map;
  EXPECT_FALSE(parse(map, document(ranges), 0));
  EXPECT_FALSE(parse(map, document(ranges), ReferencePageMap::MAX_SPINE_ENTRIES + 1));
  EXPECT_FALSE(parse(map, document(ranges) + std::string(ReferencePageMap::MAX_MANIFEST_BYTES, ' ')));
  failReferenceAllocation = true;
  EXPECT_FALSE(parse(map, document(ranges)));
  failReferenceAllocation = false;
  EXPECT_EQ(map.pageCount(), 0u);
}

TEST(ReferencePageMap, ClearsOldStateBeforeReplacingManifest) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document(ranges)));
  EXPECT_FALSE(parse(map, "{}"));
  EXPECT_EQ(map.pageCount(), 0u);
  EXPECT_TRUE(parse(map, document(ranges)));
  map.clear();
  EXPECT_EQ(map.pageCount(), 0u);
}

TEST(ReferencePageMap, ResolvesReferenceBoundariesToSpineUnits) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document(ranges)));
  auto target = map.target(3);
  ASSERT_TRUE(target);
  EXPECT_EQ(target->spineIndex, 1);
  EXPECT_EQ(target->unitOffset, 0u);
  EXPECT_EQ(target->unitCount, 100u);
  EXPECT_FALSE(target->characters);
  EXPECT_EQ(map.target(2)->unitOffset, 50u);
  EXPECT_EQ(map.target(0)->unitOffset, 0u);
  EXPECT_EQ(map.target(UINT32_MAX)->unitOffset, 50u);
  map.clear();
  EXPECT_FALSE(map.target(1));
}

TEST(ReferencePageMap, ReferenceGapsLandAtNextReadableSpine) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document("{\"wordCount\":20},{\"wordStart\":150,\"wordCount\":50}")));
  auto target = map.target(2);
  ASSERT_TRUE(target);
  EXPECT_EQ(target->spineIndex, 1);
  EXPECT_EQ(target->unitOffset, 0u);
}

TEST(ReferencePageMap, PercentTargetsPreserveFractionAndCrossSpineBoundaries) {
  ReferencePageMap map;
  EXPECT_FALSE(map.targetPercent(99));
  ASSERT_TRUE(parse(map, document(ranges)));
  EXPECT_EQ(map.targetPercent(50)->spineIndex, 1);
  EXPECT_FLOAT_EQ(map.targetPercent(50)->fraction, 0);
  EXPECT_EQ(map.targetPercent(99)->spineIndex, 1);
  EXPECT_FLOAT_EQ(map.targetPercent(99)->fraction, 0.98f);
  EXPECT_FLOAT_EQ(map.targetPercent(100)->fraction, 1);
  EXPECT_FALSE(map.targetPercent(101));
}

TEST(ReferencePageMap, PercentTargetsHandleGapsAndSubunitPositions) {
  ReferencePageMap map;
  ASSERT_TRUE(parse(map, document("{\"wordCount\":20},{\"wordStart\":150,\"wordCount\":50}")));
  EXPECT_EQ(map.targetPercent(50)->spineIndex, 1);
  EXPECT_FLOAT_EQ(map.targetPercent(50)->fraction, 0);
  const std::string tiny =
      R"({"format":"x-locations","version":1,"totalLocations":1,"totalWords":1,"wordsPerReferencePage":1,"spine":[{"wordCount":1}]})";
  ASSERT_TRUE(parse(map, tiny, 1));
  EXPECT_FLOAT_EQ(map.targetPercent(99)->fraction, 0.99f);
}
