#include <DeviceSecret.h>
#include <gtest/gtest.h>
#include <nvs.h>

TEST(DeviceSecretTest, ReadOnlyDoesNotCreateAndErrorsNeverReplaceExistingState) {
  uint8_t value[32] = {};
  EXPECT_FALSE(existingDeviceSecret(value));
  EXPECT_FALSE(fakeNvs::nameExists);
  EXPECT_EQ(fakeNvs::writes, 0u);
  EXPECT_EQ(fakeNvs::randomCalls, 0u);

  fakeNvs::failRead = true;
  EXPECT_FALSE(deviceSecret(value));
  EXPECT_EQ(fakeNvs::writes, 0u);
  EXPECT_EQ(fakeNvs::randomCalls, 0u);
  fakeNvs::failRead = false;
  fakeNvs::secretExists = true;
  fakeNvs::length = 31;
  EXPECT_FALSE(deviceSecret(value));
  EXPECT_EQ(fakeNvs::writes, 0u);
  EXPECT_EQ(fakeNvs::randomCalls, 0u);

  fakeNvs::secretExists = false;
  ASSERT_TRUE(deviceSecret(value));
  EXPECT_EQ(fakeNvs::writes, 1u);
  EXPECT_EQ(fakeNvs::commits, 1u);
  EXPECT_EQ(fakeNvs::randomCalls, 1u);
  EXPECT_EQ(value[0], 1u);
  EXPECT_EQ(value[31], 32u);
  uint8_t readOnly[32] = {};
  ASSERT_TRUE(existingDeviceSecret(readOnly));
  EXPECT_EQ(std::memcmp(value, readOnly, sizeof(value)), 0);
  EXPECT_EQ(fakeNvs::writes, 1u);
}
