#include <gtest/gtest.h>

#include <limits>

#include "personalization/CompletionPrompt.h"

TEST(CompletionPrompt, RequiresCrossingAndThenLeavingTheThresholdChapter) {
  CompletionPrompt prompt;
  prompt.configure(3, 0.8f);
  prompt.observe(2, 0.9f, false);
  EXPECT_FALSE(prompt.queueOnChapterExit(2, true));
  prompt.observe(3, 0.85f, false);
  EXPECT_FALSE(prompt.queueOnChapterExit(3, false));
  EXPECT_FALSE(prompt.queueOnChapterExit(4, true));
  EXPECT_TRUE(prompt.queueOnChapterExit(3, true));
  EXPECT_TRUE(prompt.queued());
  EXPECT_FALSE(prompt.queueOnChapterExit(3, true));
  prompt.shown();
  EXPECT_FALSE(prompt.queued());
  EXPECT_FALSE(prompt.queueOnChapterExit(3, true));
}

TEST(CompletionPrompt, OpeningAboveTheThresholdDoesNotPrompt) {
  CompletionPrompt prompt;
  prompt.configure(3, 0.8f);
  prompt.observe(3, 0.9f, false);
  prompt.observe(3, 1, false);
  EXPECT_FALSE(prompt.queueOnChapterExit(3, true));
  prompt.observe(2, 0.5f, false);
  prompt.observe(3, 0.9f, false);
  EXPECT_TRUE(prompt.queueOnChapterExit(3, true));
}

TEST(CompletionPrompt, LastPageCanCrossTheThresholdOnForwardExit) {
  CompletionPrompt prompt;
  prompt.configure(0, 0.99f);
  prompt.observe(0, 0, false);
  prompt.observe(0, 1, false);
  EXPECT_TRUE(prompt.queueOnChapterExit(0, true));
}

TEST(CompletionPrompt, AlreadyCompletedOrInvalidTargetsStayQuiet) {
  for (const float target : {-1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN()}) {
    CompletionPrompt prompt;
    prompt.configure(0, target);
    prompt.observe(0, 0, false);
    prompt.observe(0, 1, false);
    EXPECT_FALSE(prompt.queueOnChapterExit(0, true));
  }
  CompletionPrompt prompt;
  prompt.configure(0, 0.99f);
  prompt.observe(0, 0, true);
  prompt.observe(0, 1, false);
  EXPECT_FALSE(prompt.queueOnChapterExit(0, true));
}

TEST(CompletionPrompt, FailedAllocationRetriesOnlyOnAnotherChapterExit) {
  CompletionPrompt prompt;
  prompt.configure(2, 0.5f);
  prompt.observe(2, 0, false);
  prompt.observe(2, 1, false);
  ASSERT_TRUE(prompt.queueOnChapterExit(2, true));
  prompt.retryOnNextExit();
  EXPECT_FALSE(prompt.queued());
  EXPECT_TRUE(prompt.queueOnChapterExit(2, true));
}
