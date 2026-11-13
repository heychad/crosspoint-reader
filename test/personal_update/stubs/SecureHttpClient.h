#pragma once
#include <Arduino.h>

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace faketransport {
struct Response {
  int status = 200;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body = "payload";
  size_t width = 3;
  bool complete = true, tlsOk = true, beginOk = true;
  uint32_t elapsed = 0;
};
struct Request {
  std::string url, root, userAgent;
  std::vector<std::pair<std::string, std::string>> headers;
  bool reuse = true, downgrade = true;
  int follow = -1;
  uint32_t timeout = 0;
};
inline std::vector<Response> responses;
inline std::vector<Request> requests;
inline unsigned instances = 0;
}  // namespace faketransport
namespace freeink {
class SecureHttpClient {
  faketransport::Request request;
  faketransport::Response response;
  bool stopped = false, wasAborted = false;

 public:
  SecureHttpClient() { ++faketransport::instances; }
  ~SecureHttpClient() { --faketransport::instances; }
  bool begin(const std::string& url) {
    request = {};
    request.url = url;
    if (faketransport::requests.size() >= faketransport::responses.size()) return false;
    response = faketransport::responses[faketransport::requests.size()];
    return response.beginOk;
  }
  void setCACert(const char* value) { request.root = value; }
  void setTimeout(uint32_t ms) { request.timeout = ms; }
  void setReuse(bool value) { request.reuse = value; }
  void setFollowRedirects(int hops) { request.follow = hops; }
  void setAllowRedirectDowngrade(bool value) { request.downgrade = value; }
  void setUserAgent(const char* value) { request.userAgent = value; }
  void addHeader(const char* key, const char* value) { request.headers.emplace_back(key, value); }
  void end() {}
  int GET(const std::function<bool(const uint8_t*, size_t)>& sink, const std::function<bool()>& abort) {
    faketransport::requests.push_back(request);
    stopped = wasAborted = false;
    faketransport::now += response.elapsed;
    if (abort()) {
      wasAborted = true;
      return -1;
    }
    if (!response.tlsOk) return -1;
    for (size_t i = 0; i < response.body.size(); i += response.width) {
      if (abort()) {
        wasAborted = true;
        break;
      }
      if (!sink(reinterpret_cast<const uint8_t*>(response.body.data()) + i,
                std::min(response.width, response.body.size() - i))) {
        stopped = true;
        break;
      }
    }
    return response.status;
  }
  int getStatus() const { return response.status; }
  bool responseComplete() const { return response.complete && !stopped && !wasAborted; }
  bool callbackAborted() const { return stopped; }
  bool aborted() const { return wasAborted; }
  const std::string* getUniqueHeader(const char* name) const {
    const std::string* out = nullptr;
    for (const auto& [key, value] : response.headers)
      if (key == name) {
        if (out) return nullptr;
        out = &value;
      }
    return out;
  }
  std::string getHeader(const char* name) const {
    for (const auto& [key, value] : response.headers)
      if (key == name) return value;
    return "";
  }
  bool hasContentLength() const { return !getHeader("content-length").empty(); }
  size_t getContentLength() const { return std::strtoul(getHeader("content-length").c_str(), nullptr, 10); }
};
}  // namespace freeink
