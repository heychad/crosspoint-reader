#define word(...) makeWord(__VA_ARGS__)
#include <StatsSaveNotice.h>
#undef word
#include <gtest/gtest.h>

TEST(StatsSaveNotice, AcknowledgesOnlyTheObservedFailure) {
  uint32_t retained = 0;
  StatsSaveNotice notice(retained);
  notice.initialize(false);
  EXPECT_EQ(notice.pending(), 0U);
  EXPECT_FALSE(notice.acknowledge(0));
  notice.recordFailure();
  const auto first = notice.pending();
  ASSERT_NE(first, 0U);
  notice.recordFailure();
  EXPECT_FALSE(notice.acknowledge(first));
  const auto latest = notice.pending();
  EXPECT_NE(latest, first);
  EXPECT_TRUE(notice.acknowledge(latest));
  EXPECT_EQ(notice.pending(), 0U);
}

TEST(StatsSaveNotice, RetainsAcrossWakeAndClearsOnColdBoot) {
  uint32_t retained = 0;
  StatsSaveNotice beforeSleep(retained);
  beforeSleep.initialize(false);
  beforeSleep.recordFailure();
  const auto expected = beforeSleep.pending();
  StatsSaveNotice afterWake(retained);
  afterWake.initialize(true);
  EXPECT_EQ(afterWake.pending(), expected);
  afterWake.initialize(false);
  EXPECT_EQ(afterWake.pending(), 0U);
}

TEST(StatsSaveNotice, RejectsInvalidRetainedValues) {
  for (uint32_t corrupt : {0xFFFFFFFFu, 0xA7000000u, 0x12345678u}) {
    StatsSaveNotice notice(corrupt);
    notice.initialize(true);
    EXPECT_EQ(notice.pending(), 0U);
    notice.recordFailure();
    EXPECT_NE(notice.pending(), 0U);
  }
}
