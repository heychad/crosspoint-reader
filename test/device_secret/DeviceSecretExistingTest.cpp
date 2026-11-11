#include <DeviceSecret.h>
#include <gtest/gtest.h>
#include <nvs.h>

TEST(DeviceSecretExistingTest, FirstReadUsesExistingSecretWithoutAnyWrites) {
  fakeNvs::nameExists = true;
  fakeNvs::secretExists = true;
  fakeNvs::failRead = true;
  uint8_t value[32] = {};
  EXPECT_FALSE(existingDeviceSecret(value));
  EXPECT_EQ(fakeNvs::writes, 0u);
  fakeNvs::failRead = false;
  fakeNvs::length = 31;
  EXPECT_FALSE(existingDeviceSecret(value));
  EXPECT_EQ(fakeNvs::writes, 0u);
  fakeNvs::length = 32;
  for (unsigned i = 0; i < 32; ++i) fakeNvs::value[i] = static_cast<uint8_t>(i + 5);
  ASSERT_TRUE(existingDeviceSecret(value));
  EXPECT_EQ(value[0], 5u);
  EXPECT_EQ(value[31], 36u);
  EXPECT_EQ(fakeNvs::writes, 0u);
  EXPECT_EQ(fakeNvs::commits, 0u);
  EXPECT_EQ(fakeNvs::randomCalls, 0u);
  ASSERT_TRUE(deviceSecret(value));
  EXPECT_EQ(fakeNvs::writes, 0u);
}
