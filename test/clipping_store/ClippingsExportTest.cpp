#include <HalClock.h>
#include <HalStorage.h>
#include <gtest/gtest.h>

#include "clippings/ClippingsManager.h"

namespace {
constexpr auto path = ClippingsManager::CLIPPINGS_PATH;
constexpr char prior[] = "Earlier clipping\n==========\n";
constexpr char expected[] = "Book (Author)\n- Your Highlight on Page 19 | Chapter\n\nPassage\n==========\n";
bool appendClip() { return ClippingsManager::saveClipping("Book", "Author", "Chapter", 19, "Passage"); }
std::string exportedText() {
  const auto& bytes = fake::files.at(path)->bytes;
  return std::string(bytes.begin(), bytes.end());
}
}  // namespace

TEST(ClippingsExport, FailedAppendOrSyncRestoresOriginalAndAllowsRetry) {
  for (const bool syncFailure : {false, true}) {
    fake::reset();
    fake::add(path, prior);
    if (syncFailure)
      fake::failSync = 0;
    else
      fake::shortWrite = 12;
    EXPECT_FALSE(appendClip());
    EXPECT_EQ(exportedText(), prior);
    EXPECT_TRUE(appendClip());
    EXPECT_EQ(exportedText(), std::string(prior) + expected);
  }
}

TEST(ClippingsExport, FailedCloseReportsUnconfirmedExport) {
  fake::reset();
  fake::add(path, prior);
  fake::failClosePath = path;
  EXPECT_FALSE(appendClip());
  EXPECT_TRUE(fake::failureTriggered);
  EXPECT_TRUE(exportedText().starts_with(prior));
}
