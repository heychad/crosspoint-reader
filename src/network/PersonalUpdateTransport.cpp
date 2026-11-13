#include "PersonalUpdateTransport.h"

#if defined(CROSSPOINT_EXPERIMENTAL_PERSONAL_UPDATES) && CROSSPOINT_EXPERIMENTAL_PERSONAL_UPDATES
#if !defined(FREEINK_NET_WOLFSSL) || !FREEINK_NET_WOLFSSL || !defined(FREEINK_DEVICE_X4PRO) || !FREEINK_DEVICE_X4PRO
#error "Personal updates require X4 Pro and verified wolfSSL support"
#endif

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <SecureHttpClient.h>
#include <TrustedTime.h>
#include <wolfssl/wolfcrypt/settings.h>

#include <ctime>
#include <string>
#include <string_view>

#include "PersonalRelease.h"
#include "PersonalUpdatePolicy.h"
#include "PersonalUpdateTrust.h"
#include "PersonalUpdateVersion.h"
#include "WifiPowerSaveGuard.h"

#if defined(NO_ASN_TIME)
#error "Personal updates require certificate date validation"
#endif

namespace {
constexpr uint32_t SOCKET_TIMEOUT_MS = 15000, REQUEST_BUDGET_MS = 10 * 60 * 1000;
constexpr size_t MAX_IMAGE_BYTES = 7 * 1024 * 1024;
constexpr unsigned MAX_REDIRECTS = 3;
enum class RequestKind { Release, Image };

bool clockReady() {
  if (!trustedtime::trustedNow()) trustedtime::syncNow(10000);
  const int64_t floor = trustedtime::trustedNow(), wall = std::time(nullptr);
  if (floor < 1735689600LL || wall < floor || wall > 4102444800LL) {
    LOG_ERR("OTA", "No valid certificate clock");
    return false;
  }
  return true;
}

bool validFraming(const freeink::SecureHttpClient& http, size_t limit) {
  const auto* encoding = http.getUniqueHeader("content-encoding");
  if (!encoding && !http.getHeader("content-encoding").empty()) return false;
  if (encoding && *encoding != "identity") return false;
  const auto* transfer = http.getUniqueHeader("transfer-encoding");
  if (!transfer && !http.getHeader("transfer-encoding").empty()) return false;
  if (transfer && *transfer != "chunked" && *transfer != "identity") return false;
  if (!http.hasContentLength()) return true;
  if (transfer) return false;
  const auto* length = http.getUniqueHeader("content-length");
  if (!length || length->empty()) return false;
  size_t parsed = 0;
  for (const unsigned char c : *length) {
    if (c < '0' || c > '9' || parsed > limit / 10) return false;
    parsed = parsed * 10 + c - '0';
    if (parsed > limit) return false;
  }
  return parsed == http.getContentLength();
}

personalupdate::ReleaseFetch fetch(const char* url, const RequestKind kind, personalupdate::UpdateSink sink,
                                   void* context) {
  using personalupdate::ReleaseFetch;
  if (!url || !sink || !clockReady()) return ReleaseFetch::Failure;
  auto http = makeUniqueNoThrow<freeink::SecureHttpClient>();
  if (!http) {
    LOG_ERR("OTA", "OOM: %u-byte HTTPS client", static_cast<unsigned>(sizeof(freeink::SecureHttpClient)));
    return ReleaseFetch::Failure;
  }
  WifiPowerSaveGuard powerSave;
  std::string current(url);
  const auto started = millis();
  const size_t limit = kind == RequestKind::Release ? personalupdate::MAX_RELEASE_BYTES : MAX_IMAGE_BYTES;
  for (unsigned hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    const bool github = kind == RequestKind::Release ? current == personalupdate::RELEASE_API
                                                     : personalupdate::isPersonalImageUrl(current);
    if (!github && (kind != RequestKind::Image || !personalupdate::isAssetRedirect(current))) {
      LOG_ERR("OTA", "Refused update URL");
      return ReleaseFetch::Failure;
    }
    if (!clockReady() || !http->begin(current)) return ReleaseFetch::Failure;
    http->setCACert(github ? personalupdate::trust::GITHUB_ROOT : personalupdate::trust::ASSET_ROOT);
    http->setTimeout(SOCKET_TIMEOUT_MS);
    http->setReuse(false);
    http->setFollowRedirects(0);
    http->setAllowRedirectDowngrade(false);
    http->setUserAgent("CrossPoint-personal-X4Pro");
    http->addHeader("Accept-Encoding", "identity");
    http->addHeader("Accept",
                    kind == RequestKind::Release ? "application/vnd.github+json" : "application/octet-stream");
    struct Transfer {
      freeink::SecureHttpClient* http;
      personalupdate::UpdateSink sink;
      void* context;
      size_t limit, received = 0;
      bool rejected = false, checkedHeaders = false;
    } transfer{http.get(), sink, context, limit};
    const int status = http->GET(
        [&transfer](const uint8_t* data, size_t size) {
          if (transfer.http->getStatus() != 200) return false;  // Do not drain redirect/error bodies.
          if ((!transfer.checkedHeaders && !validFraming(*transfer.http, transfer.limit)) ||
              size > transfer.limit - transfer.received || !transfer.sink(transfer.context, data, size)) {
            transfer.rejected = true;
            return false;
          }
          transfer.checkedHeaders = true;
          transfer.received += size;
          return true;
        },
        [started] { return static_cast<uint32_t>(millis() - started) >= REQUEST_BUDGET_MS; });
    if (static_cast<uint32_t>(millis() - started) >= REQUEST_BUDGET_MS || http->aborted()) return ReleaseFetch::Failure;
    if (kind == RequestKind::Release && status == 404) return ReleaseFetch::NotFound;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
      if (kind != RequestKind::Image || hop == MAX_REDIRECTS) return ReleaseFetch::Failure;
      const auto* location = http->getUniqueHeader("location");
      if (!location || !personalupdate::isAssetRedirect(*location)) return ReleaseFetch::Failure;
      current = *location;
      http->end();
      continue;
    }
    if (status != 200 || transfer.rejected || !validFraming(*http, limit) || !http->responseComplete() ||
        http->callbackAborted())
      return ReleaseFetch::Failure;
    return ReleaseFetch::Success;
  }
  return ReleaseFetch::Failure;
}
}  // namespace

bool personalupdate::transportAvailable() { return true; }
personalupdate::ReleaseFetch personalupdate::fetchRelease(UpdateSink sink, void* context) {
  const auto result = fetch(RELEASE_API, RequestKind::Release, sink, context);
  if (result == ReleaseFetch::Failure) LOG_ERR("OTA", "Verified release request failed");
  return result;
}
bool personalupdate::fetchImage(const char* url, UpdateSink sink, void* context) {
  const bool ok =
      url && isPersonalImageUrl(url) && fetch(url, RequestKind::Image, sink, context) == ReleaseFetch::Success;
  if (!ok) LOG_ERR("OTA", "Verified image request failed");
  return ok;
}
#else
bool personalupdate::transportAvailable() { return false; }
personalupdate::ReleaseFetch personalupdate::fetchRelease(UpdateSink, void*) { return ReleaseFetch::Failure; }
bool personalupdate::fetchImage(const char*, UpdateSink, void*) { return false; }
#endif
