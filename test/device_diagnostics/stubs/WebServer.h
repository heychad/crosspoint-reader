#pragma once

#include <map>
#include <string>

class WebServer {
 public:
  std::map<std::string, std::string> headers;
  std::string contentType, body;
  int status = 0;
  void sendHeader(const char* name, const char* value) { headers[name] = value; }
  void send(int statusCode, const char* type, const char* data);
};
