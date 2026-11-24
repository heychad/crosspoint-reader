#pragma once

#include <WebServer.h>

#include "util/BookMove.h"

namespace bookmove {
inline void sendResult(WebServer& server, Result result, int success = 200) {
  switch (result) {
    case Result::Moved:
      server.send(success, "text/plain", "Moved with reading state preserved");
      return;
    case Result::NothingPending:
      server.send(200, "text/plain", "No book move needs recovery");
      return;
    case Result::Invalid:
      server.send(400, "text/plain", "Invalid book move; keep the file extension unchanged");
      return;
    case Result::Conflict:
      server.send(409, "text/plain", "Book or reading-state conflict; files retained");
      return;
    case Result::Busy:
      server.send(423, "text/plain", "Close the reader and finish other file operations, then retry");
      return;
    case Result::Unavailable:
      server.send(503, "text/plain", "Book move unavailable; retry with storage idle");
      return;
    case Result::InsufficientSpace:
      server.send(507, "text/plain", "Free space on the SD card and retry the book move");
      return;
    case Result::RecoveryRequired:
      server.send(423, "text/plain", "Book move needs recovery; files retained. Restart the reader or retry recovery");
      return;
  }
}

// Registered before normal routes: selection occurs before multipart/raw
// streaming, so a pending transaction cannot be edited by upload callbacks.
class RecoveryGuard final : public RequestHandler {
 public:
  bool canHandle(WebServer&, HTTPMethod method, const String& uri) override {
    if (method == HTTP_GET || method == HTTP_HEAD || method == HTTP_OPTIONS || method == HTTP_PROPFIND) return false;
    if (method == HTTP_POST && uri == "/api/books/move/recover") return false;
    return pending();
  }
  bool canUpload(WebServer&, const String&) override { return true; }
  bool canRaw(WebServer&, const String&) override { return true; }
  bool handle(WebServer& server, HTTPMethod, const String&) override {
    sendResult(server, Result::RecoveryRequired);
    return true;
  }
};
}  // namespace bookmove
