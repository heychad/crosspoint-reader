#include <PersonalUpdateTransport.h>
#include <PersonalUpdateVersion.h>
#include <gtest/gtest.h>

TEST(PersonalUpdateVersion, ReleaseEndpointBelongsToPersonalFork) {
  EXPECT_STREQ(personalupdate::RELEASE_API, "https://api.github.com/repos/heychad/crosspoint-reader/releases/latest");
}

TEST(PersonalUpdateVersion, OnlineUpdatesStayDisabledUntilAuthenticationIsImplemented) {
  EXPECT_FALSE(personalupdate::transportAvailable());
}

TEST(PersonalUpdateVersion, OnlyNewerPersonalReleaseIsOffered) {
  using personalupdate::isNewer;
  EXPECT_TRUE(isNewer("2.0.0-chad.4", "2.0.0-chad.3-x4pro"));
  EXPECT_TRUE(isNewer("2.0.0-chad.5", "2.0.0-chad.4"));
  EXPECT_TRUE(isNewer("2.0.0-chad.5", "2.0.0-chad.4-test19"));
  EXPECT_FALSE(isNewer("2.0.0-chad.3", "2.0.0-chad.3-test19-x4pro"));
  EXPECT_TRUE(isNewer("2.1.0-chad.1", "2.0.0-chad.99-x4pro"));
  EXPECT_FALSE(isNewer("2.0.0-chad.3", "2.0.0-chad.3-x4pro"));
  EXPECT_FALSE(isNewer("2.0.0-chad.2", "2.0.0-chad.3-test19-x4pro"));
  EXPECT_FALSE(isNewer("1.9.9-chad.100", "2.0.0-chad.3-x4pro"));
}

TEST(PersonalUpdateVersion, RejectsUpstreamMalformedAndOtherBoardTags) {
  using personalupdate::isNewer;
  for (const auto* tag : {"2.1.0", "v2.1.0-chad.5", "2.1.0-chad.5-rc", "2.1.0-chad.5-x4pro", "2.1.0-chad.",
                          "2.1.0-chad.4294967296", "2.1.0-chad.5garbage", "", "2.0"}) {
    EXPECT_FALSE(isNewer(tag, "2.0.0-chad.3-x4pro")) << tag;
  }
  for (const auto* current : {"2.0.0", "2.0.0-chad.3-x4", "2.0.0-chad.3-test-x4pro", "2.0.0-chad.3-x4pro-extra", ""}) {
    EXPECT_FALSE(isNewer("2.1.0-chad.1", current)) << current;
  }
}
