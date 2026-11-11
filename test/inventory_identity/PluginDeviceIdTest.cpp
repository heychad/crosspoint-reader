#include <DeviceSecret.h>
#include <gtest/gtest.h>
#include <wolfssl/wolfcrypt/hash.h>

#include <cstring>
#include <string>

#include "network/PluginDeviceId.h"

TEST(PluginDeviceIdTest, HashesDeviceSecretAndExactPluginScope) {
  fake::failSecret = fake::failHash = false;
  char result[65] = {};
  constexpr char SCOPE[] = "xteink-book-sync";
  ASSERT_TRUE(pluginidentity::deviceId(SCOPE, sizeof(SCOPE) - 1, result));
  EXPECT_EQ(std::strlen(result), 64u);
  ASSERT_EQ(fake::hashInput.size(), 32u + sizeof(SCOPE) - 1);
  for (unsigned i = 0; i < 32; ++i) EXPECT_EQ(fake::hashInput[i], i);
  EXPECT_EQ(std::string(fake::hashInput.begin() + 32, fake::hashInput.end()), SCOPE);
  for (const char ch : std::string(result)) EXPECT_TRUE((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
}

TEST(PluginDeviceIdTest, RejectsInvalidScopeAndCryptoFailures) {
  char result[65] = {};
  fake::failSecret = fake::failHash = false;
  EXPECT_FALSE(pluginidentity::deviceId(nullptr, 1, result));
  EXPECT_FALSE(pluginidentity::deviceId("", 0, result));
  EXPECT_FALSE(pluginidentity::deviceId("x", 65, result));
  fake::failSecret = true;
  EXPECT_FALSE(pluginidentity::deviceId("x", 1, result));
  fake::failSecret = false;
  fake::failHash = true;
  EXPECT_FALSE(pluginidentity::deviceId("x", 1, result));
  fake::failHash = false;
}

TEST(PluginDeviceIdTest, ReadOnlyIdentityNeverCreatesASecret) {
  fake::createCalls = fake::readOnlyCalls = 0;
  fake::failExistingSecret = true;
  char result[65] = {};
  EXPECT_FALSE(pluginidentity::deviceId("x", 1, result, false));
  EXPECT_EQ(fake::createCalls, 0u);
  EXPECT_EQ(fake::readOnlyCalls, 1u);
  fake::failExistingSecret = false;
  EXPECT_TRUE(pluginidentity::deviceId("x", 1, result, false));
  EXPECT_EQ(fake::createCalls, 0u);
  EXPECT_EQ(fake::readOnlyCalls, 2u);
}
