#include <PersonalUpdatePolicy.h>
#include <PersonalUpdateTransport.h>
#include <PersonalUpdateTrust.h>
#include <PersonalUpdateVersion.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <esp_wifi.h>
#include <gtest/gtest.h>

#include <ctime>
#include <new>

namespace {
using personalupdate::ReleaseFetch;
using namespace faketransport;
constexpr const char* IMAGE_URL =
    "https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.4-x4pro.bin";
constexpr const char* CDN_URL =
    "https://release-assets.githubusercontent.com/github-production-release-asset/1/2?sig=foo";
bool failClientAllocation = false;
bool capture(void* context, const uint8_t* bytes, size_t count) {
  static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(bytes), count);
  return true;
}
class PersonalTransport : public testing::Test {
 protected:
  std::string output;
  void SetUp() override {
    failClientAllocation = false;
    now = syncCalls = 0;
    faketransport::floor = std::time(nullptr);
    syncWorks = true;
    responses = {Response{}};
    requests.clear();
    powerSave = WIFI_PS_MIN_MODEM;
  }
  void TearDown() override {
    EXPECT_EQ(instances, 0u);
    EXPECT_EQ(powerSave, WIFI_PS_MIN_MODEM);
  }
  ReleaseFetch release() { return personalupdate::fetchRelease(capture, &output); }
  bool image(const char* url = IMAGE_URL) { return personalupdate::fetchImage(url, capture, &output); }
};
}  // namespace

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (failClientAllocation && size == sizeof(freeink::SecureHttpClient)) return nullptr;
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}
int64_t trustedtime::trustedNow() { return faketransport::floor; }
bool trustedtime::syncNow(uint32_t timeout) {
  EXPECT_EQ(timeout, 10000u);
  ++faketransport::syncCalls;
  if (faketransport::syncWorks) faketransport::floor = std::time(nullptr);
  return faketransport::syncWorks;
}

TEST_F(PersonalTransport, ExactReleaseUsesPinnedRootWithoutRedirectsCredentialsOrDowngrade) {
  EXPECT_TRUE(personalupdate::transportAvailable());
  ASSERT_EQ(release(), ReleaseFetch::Success);
  EXPECT_EQ(output, "payload");
  ASSERT_EQ(requests.size(), 1u);
  const auto& r = requests[0];
  EXPECT_EQ(r.url, personalupdate::RELEASE_API);
  EXPECT_EQ(r.root, personalupdate::trust::GITHUB_ROOT);
  EXPECT_EQ(r.timeout, 15000u);
  EXPECT_EQ(r.follow, 0);
  EXPECT_FALSE(r.reuse);
  EXPECT_FALSE(r.downgrade);
  EXPECT_FALSE(r.userAgent.empty());
  EXPECT_EQ(r.headers.size(), 2u);
  for (const auto& h : r.headers) EXPECT_NE(h.first, "Authorization");
}

TEST_F(PersonalTransport, ImageRedirectReauthenticatesOnlyAllowedCdnWithItsOwnRoot) {
  Response redirect;
  redirect.status = 302;
  redirect.headers = {{"location", CDN_URL}};
  redirect.body = "ignored body";
  responses = {redirect, Response{}};
  EXPECT_TRUE(image());
  EXPECT_EQ(output, "payload");
  ASSERT_EQ(requests.size(), 2u);
  EXPECT_EQ(requests[0].root, personalupdate::trust::GITHUB_ROOT);
  EXPECT_EQ(requests[1].root, personalupdate::trust::ASSET_ROOT);
  EXPECT_EQ(requests[1].url, CDN_URL);
}

TEST_F(PersonalTransport, InitialImageUrlRequiresExactPersonalAsset) {
  EXPECT_TRUE(personalupdate::isPersonalImageUrl(IMAGE_URL));
  for (const auto* bad :
       {"http://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.4-x4pro.bin",
        "https://github.com.evil/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/"
        "crosspoint-2.0.0-chad.4-x4pro.bin",
        "https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.5-x4pro.bin",
        "https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4/crosspoint-2.0.0-chad.4-x4.bin",
        "https://github.com/heychad/crosspoint-reader/releases/download/2.0.0-chad.4-rc/"
        "crosspoint-2.0.0-chad.4-rc-x4pro.bin",
        CDN_URL, ""})
    EXPECT_FALSE(image(bad)) << bad;
  for (const auto* suffix : {"?extra=1", "#fragment", "/suffix", "\r\nInjected: x"})
    EXPECT_FALSE(image((std::string(IMAGE_URL) + suffix).c_str()));
  EXPECT_FALSE(image(nullptr));
  EXPECT_TRUE(requests.empty());
}

TEST_F(PersonalTransport, RedirectPolicyRejectsUnexpectedOriginsAndAmbiguousLocations) {
  for (const auto* bad :
       {"http://release-assets.githubusercontent.com/file", "//release-assets.githubusercontent.com/file",
        "https://release-assets.githubusercontent.com.evil/file",
        "https://release-assets.githubusercontent.com:443/file",
        "https://user@release-assets.githubusercontent.com/file",
        "https://release-assets.githubusercontent.com@evil/file", "https://release-assets.githubusercontent.com/",
        "https://release-assets.githubusercontent.com/?query",
        "https://release-assets.githubusercontent.com/file#fragment",
        "https://release-assets.githubusercontent.com/\r\nX: y", "https://release-assets.githubusercontent.com/\\evil",
        "https://objects.githubusercontent.com/file", IMAGE_URL}) {
    requests.clear();
    responses = {Response{}};
    responses[0].status = 302;
    responses[0].headers = {{"location", bad}};
    EXPECT_FALSE(image()) << bad;
    EXPECT_EQ(requests.size(), 1u);
  }
  responses[0].headers = {{"location", CDN_URL}, {"location", CDN_URL}};
  requests.clear();
  EXPECT_FALSE(image());
  EXPECT_EQ(requests.size(), 1u);
  responses[0].headers = {{"location", std::string(CDN_URL) + std::string(2048, 'x')}};
  requests.clear();
  EXPECT_FALSE(image());
  EXPECT_EQ(requests.size(), 1u);
}

TEST_F(PersonalTransport, RedirectStatusAndHopLimitsAreExplicit) {
  for (int status : {301, 302, 303, 307, 308}) {
    requests.clear();
    output.clear();
    responses = {Response{}, Response{}};
    responses[0].status = status;
    responses[0].headers = {{"location", CDN_URL}};
    EXPECT_TRUE(image());
    EXPECT_EQ(output, "payload");
  }
  requests.clear();
  responses.assign(5, Response{});
  for (auto& r : responses) {
    r.status = 302;
    r.headers = {{"location", CDN_URL}};
  }
  EXPECT_FALSE(image());
  EXPECT_EQ(requests.size(), 4u);
  requests.clear();
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_EQ(requests.size(), 1u);
}

TEST_F(PersonalTransport, MissingReleaseIsNoUpdateButOtherHttpFailuresStayFailures) {
  responses[0].status = 404;
  EXPECT_EQ(release(), ReleaseFetch::NotFound);
  EXPECT_TRUE(output.empty());
  requests.clear();
  EXPECT_FALSE(image());
  for (int status : {0, 201, 204, 206, 400, 401, 403, 429, 500}) {
    requests.clear();
    responses[0].status = status;
    EXPECT_EQ(release(), ReleaseFetch::Failure) << status;
  }
}

TEST_F(PersonalTransport, InvalidClockAndFailedSyncCannotStartNetwork) {
  faketransport::floor = 0;
  syncWorks = false;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_EQ(syncCalls, 1u);
  EXPECT_TRUE(requests.empty());
  syncWorks = true;
  EXPECT_EQ(release(), ReleaseFetch::Success);
  EXPECT_EQ(syncCalls, 2u);
  requests.clear();
  faketransport::floor = std::time(nullptr) + 60;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_TRUE(requests.empty());
  faketransport::floor = 4102444801LL;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_TRUE(requests.empty());
}

TEST_F(PersonalTransport, ClientAllocationAndTlsFailuresReleaseResourcesWithoutRetryingInsecurely) {
  failClientAllocation = true;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_TRUE(requests.empty());
  failClientAllocation = false;
  responses[0].tlsOk = false;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_EQ(requests.size(), 1u);
  EXPECT_TRUE(output.empty());
  requests.clear();
  responses[0].tlsOk = true;
  responses[0].beginOk = false;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_TRUE(requests.empty());
}

TEST_F(PersonalTransport, BodyCapsAndStrictFramingRejectBeforeDeliveringInvalidBytes) {
  for (auto headers : std::vector<std::vector<std::pair<std::string, std::string>>>{
           {{"content-length", "65537"}},
           {{"content-length", "+7"}},
           {{"content-length", "7junk"}},
           {{"content-length", "7"}, {"content-length", "7"}},
           {{"content-length", "7"}, {"transfer-encoding", "chunked"}},
           {{"content-encoding", "gzip"}},
           {{"content-encoding", "identity"}, {"content-encoding", "identity"}},
           {{"transfer-encoding", "gzip"}},
           {{"transfer-encoding", "chunked"}, {"transfer-encoding", "chunked"}},
           {{"content-length", std::string(100, '9')}}}) {
    requests.clear();
    output.clear();
    responses[0].headers = std::move(headers);
    EXPECT_EQ(release(), ReleaseFetch::Failure);
    EXPECT_TRUE(output.empty());
  }
  requests.clear();
  responses[0].headers.clear();
  responses[0].body.assign(65537, 'x');
  responses[0].width = 65537;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_TRUE(output.empty());
  requests.clear();
  responses[0].body.assign(65536, 'x');
  responses[0].width = 2000;
  EXPECT_EQ(release(), ReleaseFetch::Success);
  EXPECT_EQ(output.size(), 65536u);
}

TEST_F(PersonalTransport, CompleteFramedResponsesWorkButTruncationAndSinkStopsFail) {
  for (auto headers : std::vector<std::vector<std::pair<std::string, std::string>>>{
           {{"content-length", "7"}}, {{"transfer-encoding", "chunked"}}, {{"content-encoding", "identity"}}}) {
    requests.clear();
    output.clear();
    responses[0].headers = std::move(headers);
    EXPECT_EQ(release(), ReleaseFetch::Success);
    EXPECT_EQ(output, "payload");
  }
  requests.clear();
  responses[0].complete = false;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  requests.clear();
  responses[0].complete = true;
  EXPECT_EQ(personalupdate::fetchRelease([](void*, const uint8_t*, size_t) { return false; }, nullptr),
            ReleaseFetch::Failure);
  requests.clear();
  EXPECT_EQ(personalupdate::fetchRelease(nullptr, nullptr), ReleaseFetch::Failure);
  EXPECT_TRUE(requests.empty());
}

TEST_F(PersonalTransport, CooperativeBudgetAppliesAcrossRedirectsAndClockWrap) {
  responses[0].elapsed = 600000;
  EXPECT_EQ(release(), ReleaseFetch::Failure);
  EXPECT_TRUE(output.empty());
  requests.clear();
  responses = {Response{}, Response{}};
  responses[0].status = 302;
  responses[0].headers = {{"location", CDN_URL}};
  responses[0].elapsed = 350000;
  responses[1].elapsed = 250000;
  EXPECT_FALSE(image());
  EXPECT_EQ(requests.size(), 2u);
  requests.clear();
  responses = {Response{}};
  now = 0xfffffff0u;
  responses[0].elapsed = 32;
  EXPECT_EQ(release(), ReleaseFetch::Success);
}
