#include "WebDAVHandler.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <HiddenItems.h>
#include <Logging.h>
#include <esp_random.h>

#include <algorithm>

#include "BookMoveWeb.h"
#include "ClippingStore.h"
#include "ProtectedPaths.h"
#include "util/BookCacheUtils.h"
#include "util/TaskWatchdog.h"

namespace {
// RFC 1123 date format helper: "Sun, 06 Nov 1994 08:49:37 GMT"
// ESP32 doesn't have real-time clock set by default, so we use a fixed epoch date
// as a fallback. The date is not critical for WebDAV Class 1 operations.
const char* FIXED_DATE = "Thu, 01 Jan 2024 00:00:00 GMT";
}  // namespace

// ── RequestHandler interface ─────────────────────────────────────────────────

bool WebDAVHandler::canHandle(WebServer& server, HTTPMethod method, const String& uri) {
  (void)server;
  (void)uri;
  switch (method) {
    case HTTP_OPTIONS:
    case HTTP_PROPFIND:
    case HTTP_GET:
    case HTTP_HEAD:
    case HTTP_PUT:
    case HTTP_DELETE:
    case HTTP_MKCOL:
    case HTTP_MOVE:
    case HTTP_COPY:
    case HTTP_LOCK:
    case HTTP_UNLOCK:
      return true;
    default:
      return false;
  }
}

bool WebDAVHandler::canRaw(WebServer& server, const String& uri) {
  (void)uri;
  return server.method() == HTTP_PUT;
}

void WebDAVHandler::raw(WebServer& server, const String& uri, HTTPRaw& raw) {
  (void)uri;
  if (raw.status == RAW_START) {
    _putPath = getRequestPath(server);
    if (isProtectedPath(_putPath) ||
        protectedpaths::isSensitivePath(_putPath.c_str(), protectedpaths::PathAccess::Write)) {
      _putOk = false;
      _putPath = "";
      return;
    }

    // Ensure parent directory exists
    int lastSlash = _putPath.lastIndexOf('/');
    if (lastSlash > 0) {
      String parentPath = _putPath.substring(0, lastSlash);
      if (!Storage.exists(parentPath.c_str())) {
        _putOk = false;
        return;
      }
    }

    if (_putFile) _putFile.close();
    _putExisted = Storage.exists(_putPath.c_str());

    if (_putExisted) {
      HalFile existing = Storage.open(_putPath.c_str());
      if (existing && existing.isDirectory()) {
        existing.close();
        _putOk = false;
        return;
      }
      if (existing) existing.close();
    }

    // Write to a temp file to avoid destroying the original on failed upload
    String tempPath = _putPath + ".davtmp";
    Storage.remove(tempPath.c_str());
    _putOk = Storage.openFileForWrite("DAV", tempPath, _putFile);
    LOG_DBG("DAV", "PUT START: %s", _putPath.c_str());

  } else if (raw.status == RAW_WRITE) {
    if (_putFile && _putOk) {
      resetTaskWatchdogIfSubscribed();
      size_t written = _putFile.write(raw.buf, raw.currentSize);
      if (written != raw.currentSize) {
        _putOk = false;
      }
    }

  } else if (raw.status == RAW_END) {
    if (_putFile) _putFile.close();
    if (_putOk) {
      String tempPath = _putPath + ".davtmp";
      if (_putExisted) Storage.remove(_putPath.c_str());
      HalFile tmp = Storage.open(tempPath.c_str());
      if (tmp) {
        _putOk = tmp.rename(_putPath.c_str());
        tmp.close();
      } else {
        _putOk = false;
      }
      if (!_putOk) Storage.remove(tempPath.c_str());
    }
    LOG_DBG("DAV", "PUT END: %u bytes, ok=%d", raw.totalSize, _putOk);

  } else if (raw.status == RAW_ABORTED) {
    if (_putFile) _putFile.close();
    if (!_putPath.isEmpty()) {
      String tempPath = _putPath + ".davtmp";
      Storage.remove(tempPath.c_str());
    }
    _putOk = false;
  }
}

bool WebDAVHandler::handle(WebServer& server, HTTPMethod method, const String& uri) {
  (void)uri;
  switch (method) {
    case HTTP_OPTIONS:
      handleOptions(server);
      return true;
    case HTTP_PROPFIND:
      handlePropfind(server);
      return true;
    case HTTP_GET:
      handleGet(server);
      return true;
    case HTTP_HEAD:
      handleHead(server);
      return true;
    case HTTP_PUT:
      handlePut(server);
      return true;
    case HTTP_DELETE:
      handleDelete(server);
      return true;
    case HTTP_MKCOL:
      handleMkcol(server);
      return true;
    case HTTP_MOVE:
      handleMove(server);
      return true;
    case HTTP_COPY:
      handleCopy(server);
      return true;
    case HTTP_LOCK:
      handleLock(server);
      return true;
    case HTTP_UNLOCK:
      handleUnlock(server);
      return true;
    default:
      return false;
  }
}

// ── OPTIONS ──────────────────────────────────────────────────────────────────

void WebDAVHandler::handleOptions(WebServer& s) {
  s.sendHeader("DAV", "1");
  s.sendHeader("Allow",
               "OPTIONS, GET, HEAD, PUT, DELETE, "
               "PROPFIND, MKCOL, MOVE, COPY, LOCK, UNLOCK");
  s.sendHeader("MS-Author-Via", "DAV");
  s.send(200);
  LOG_DBG("DAV", "OPTIONS %s", s.uri().c_str());
}

// ── PROPFIND ─────────────────────────────────────────────────────────────────

void WebDAVHandler::handlePropfind(WebServer& s) {
  String path = getRequestPath(s);
  int depth = getDepth(s);

  LOG_DBG("DAV", "PROPFIND %s depth=%d", path.c_str(), depth);

  // Check if path exists
  if (!Storage.exists(path.c_str()) && path != "/") {
    s.send(404, "text/plain", "Not Found");
    return;
  }

  HalFile root = Storage.open(path.c_str());
  if (!root) {
    if (path == "/") {
      // Root should always work — send minimal response
      s.setContentLength(CONTENT_LENGTH_UNKNOWN);
      s.send(207, "application/xml; charset=\"utf-8\"", "");
      s.sendContent(
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
          "<D:multistatus xmlns:D=\"DAV:\">\n");
      sendPropEntry(s, "/", true, 0, FIXED_DATE);
      s.sendContent("</D:multistatus>\n");
      s.sendContent("");
      return;
    }
    s.send(500, "text/plain", "Failed to open");
    return;
  }

  bool isDir = root.isDirectory();

  s.setContentLength(CONTENT_LENGTH_UNKNOWN);
  s.send(207, "application/xml; charset=\"utf-8\"", "");
  s.sendContent(
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<D:multistatus xmlns:D=\"DAV:\">\n");

  // Entry for the resource itself
  if (isDir) {
    sendPropEntry(s, path, true, 0, FIXED_DATE);
  } else {
    sendPropEntry(s, path, false, root.size(), FIXED_DATE);
    root.close();
    s.sendContent("</D:multistatus>\n");
    s.sendContent("");
    return;
  }

  // If depth > 0 and it's a directory, list children
  if (depth > 0) {
    HalFile file = root.openNextFile();
    char name[500];
    while (file) {
      file.getName(name, sizeof(name));
      String fileName(name);

      // Skip hidden/protected items
      bool shouldHide = fileName.startsWith(".");
      if (!shouldHide) {
        for (const auto* item : FsHelpers::HIDDEN_ITEMS) {
          if (fileName.equals(item)) {
            shouldHide = true;
            break;
          }
        }
      }

      if (!shouldHide) {
        String childPath = path;
        if (!childPath.endsWith("/")) childPath += "/";
        childPath += fileName;

        if (file.isDirectory()) {
          sendPropEntry(s, childPath, true, 0, FIXED_DATE);
        } else {
          sendPropEntry(s, childPath, false, file.size(), FIXED_DATE);
        }
      }

      file.close();
      yield();
      resetTaskWatchdogIfSubscribed();
      file = root.openNextFile();
    }
  }

  root.close();
  s.sendContent("</D:multistatus>\n");
  s.sendContent("");
}

void WebDAVHandler::sendPropEntry(WebServer& s, const String& path, bool isDir, size_t size,
                                  const String& lastModified) const {
  String href;
  urlEncodePath(path, href);
  // Ensure directory hrefs end with /
  if (isDir && !href.endsWith("/")) href += "/";

  String xml = "<D:response><D:href>";
  xml += href;
  xml += "</D:href><D:propstat><D:prop>";

  if (isDir) {
    xml += "<D:resourcetype><D:collection/></D:resourcetype>";
  } else {
    xml += "<D:resourcetype/>";
    xml += "<D:getcontentlength>";
    xml += String(size);
    xml += "</D:getcontentlength>";
    String mime = getMimeType(path);
    xml += "<D:getcontenttype>";
    xml += mime;
    xml += "</D:getcontenttype>";
  }

  xml += "<D:getlastmodified>";
  xml += lastModified;
  xml += "</D:getlastmodified>";

  xml += "</D:prop><D:status>HTTP/1.1 200 OK</D:status></D:propstat></D:response>\n";

  s.sendContent(xml);
}

// ── GET ──────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleGet(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "GET %s", path.c_str());

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!Storage.exists(path.c_str())) {
    s.send(404, "text/plain", "Not Found");
    return;
  }

  HalFile file = Storage.open(path.c_str());
  if (!file) {
    s.send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    // For directories, return a PROPFIND-like response or redirect
    s.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String contentType = getMimeType(path);
  const size_t fileSize = file.size();
  file.close();

  s.setContentLength(fileSize);
  s.send(200, contentType.c_str(), "");

  // **This used to be `client.write(file)`, and it sent exactly one byte.**
  //
  // It worked while `Storage.open()` returned an SdFat `FsFile`, which derives
  // from `Stream`: the call matched `NetworkClient::write(Stream&)` and streamed
  // the file. `HalFile` derives from `Print`, not `Stream` (lib/hal/HalStorage.h),
  // so that overload stopped matching, the compiler took `HalFile::operator bool()`
  // instead, promoted the `true` to `uint8_t`, and wrote a single `0x01`. **The
  // call site never changed; its meaning did**, and nothing warned because every
  // step is a legal conversion.
  //
  // The reply still carried the real Content-Length, so a client waited for a
  // body that never arrived and reported a truncated transfer rather than an
  // error, which is why this reads as a flaky network rather than a server bug.
  NetworkClient client = s.client();
  if (!Storage.readFileToStream(path.c_str(), client)) {
    LOG_ERR("DAV", "GET %s: failed to stream file", path.c_str());
  }
}

// ── HEAD ─────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleHead(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "HEAD %s", path.c_str());

  if (isProtectedPath(path)) {
    s.send(403, "text/plain", "");
    return;
  }

  if (!Storage.exists(path.c_str())) {
    s.send(404, "text/plain", "");
    return;
  }

  HalFile file = Storage.open(path.c_str());
  if (!file) {
    s.send(500, "text/plain", "");
    return;
  }

  if (file.isDirectory()) {
    file.close();
    s.send(200, "text/html", "");
    return;
  }

  String contentType = getMimeType(path);
  s.setContentLength(file.size());
  s.send(200, contentType.c_str(), "");
  file.close();
}

// ── PUT ──────────────────────────────────────────────────────────────────────

void WebDAVHandler::handlePut(WebServer& s) {
  // Body was already received via canRaw/raw callbacks
  String path = getRequestPath(s);
  LOG_DBG("DAV", "PUT %s", path.c_str());

  if (isProtectedPath(path) || protectedpaths::isSensitivePath(path.c_str(), protectedpaths::PathAccess::Write)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!_putOk) {
    String tempPath = path + ".davtmp";
    Storage.remove(tempPath.c_str());
    s.send(500, "text/plain", "Write failed - incomplete upload or disk full");
    return;
  }

  clearBookCache(path.c_str());
  s.send(_putExisted ? 204 : 201);
  LOG_DBG("DAV", "PUT complete: %s", path.c_str());
}

// ── DELETE ───────────────────────────────────────────────────────────────────

void WebDAVHandler::handleDelete(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "DELETE %s", path.c_str());

  if (path == "/" || path.isEmpty()) {
    s.send(403, "text/plain", "Cannot delete root");
    return;
  }

  if (isProtectedPath(path) || protectedpaths::isSensitivePath(path.c_str(), protectedpaths::PathAccess::Remove)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (!Storage.exists(path.c_str())) {
    s.send(404, "text/plain", "Not Found");
    return;
  }

  HalFile file = Storage.open(path.c_str());
  if (!file) {
    s.send(500, "text/plain", "Failed to open");
    return;
  }

  if (file.isDirectory()) {
    // Check if directory is empty
    HalFile entry = file.openNextFile();
    if (entry) {
      entry.close();
      file.close();
      s.send(409, "text/plain", "Directory not empty");
      return;
    }
    file.close();
    if (Storage.rmdir(path.c_str())) {
      s.send(204);
    } else {
      s.send(500, "text/plain", "Failed to remove directory");
    }
  } else {
    file.close();
    if (removeBookFile(path.c_str())) {
      s.send(204);
    } else {
      s.send(500, "text/plain", "Failed to delete file");
    }
  }
}

// ── MKCOL ────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleMkcol(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "MKCOL %s", path.c_str());

  if (isProtectedPath(path) || protectedpaths::isSensitivePath(path.c_str(), protectedpaths::PathAccess::Write)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  // MKCOL must not have a body (RFC 4918)
  if (s.clientContentLength() > 0) {
    s.send(415, "text/plain", "Unsupported Media Type");
    return;
  }

  if (Storage.exists(path.c_str())) {
    s.send(405, "text/plain", "Already exists");
    return;
  }

  // Check parent exists
  int lastSlash = path.lastIndexOf('/');
  if (lastSlash > 0) {
    String parentPath = path.substring(0, lastSlash);
    if (!parentPath.isEmpty() && !Storage.exists(parentPath.c_str())) {
      s.send(409, "text/plain", "Parent directory does not exist");
      return;
    }
  }

  if (Storage.mkdir(path.c_str())) {
    s.send(201);
    LOG_DBG("DAV", "Created directory: %s", path.c_str());
  } else {
    s.send(500, "text/plain", "Failed to create directory");
  }
}

// ── MOVE ─────────────────────────────────────────────────────────────────────

void WebDAVHandler::handleMove(WebServer& s) {
  String srcPath = getRequestPath(s);
  String dstPath = getDestinationPath(s);
  bool overwrite = getOverwrite(s);

  LOG_DBG("DAV", "MOVE %s -> %s (overwrite=%d)", srcPath.c_str(), dstPath.c_str(), overwrite);

  if (srcPath == "/" || srcPath.isEmpty()) {
    s.send(403, "text/plain", "Cannot move root");
    return;
  }

  if (isProtectedPath(srcPath) || isProtectedPath(dstPath) ||
      protectedpaths::isSensitivePath(srcPath.c_str(), protectedpaths::PathAccess::Remove) ||
      protectedpaths::isSensitivePath(dstPath.c_str(), protectedpaths::PathAccess::Write)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (dstPath.isEmpty()) {
    s.send(400, "text/plain", "Missing Destination header");
    return;
  }

  if (srcPath == dstPath) {
    s.send(204);
    return;
  }

  if (!Storage.exists(srcPath.c_str())) {
    s.send(404, "text/plain", "Source not found");
    return;
  }

  // Check destination parent exists
  int lastSlash = dstPath.lastIndexOf('/');
  if (lastSlash > 0) {
    String parentPath = dstPath.substring(0, lastSlash);
    if (!parentPath.isEmpty() && !Storage.exists(parentPath.c_str())) {
      s.send(409, "text/plain", "Destination parent does not exist");
      return;
    }
  }

  HalFile source = Storage.openChecked(srcPath.c_str());
  if (!source) {
    s.send(503, "text/plain", "Cannot inspect source");
    return;
  }
  const bool directory = source.isDirectory();
  if (!source.close()) {
    s.send(503, "text/plain", "Cannot close source");
    return;
  }
  if (directory) {
    s.send(409, "text/plain", "Move books individually to preserve their reading state");
    return;
  }
  if (bookmove::managedBook(srcPath.c_str()) || bookmove::managedBook(dstPath.c_str())) {
    // A book's destination and state must never be removed for DAV Overwrite.
    if (Storage.lookupPath(dstPath.c_str()) != HalStorage::PathStatus::Missing) {
      s.send(overwrite ? 409 : 412, "text/plain", "Destination retained; book overwrite is not supported");
      return;
    }
    bookmove::sendResult(s, bookmove::move(srcPath.c_str(), dstPath.c_str()), 201);
    return;
  }

  bool dstExists = Storage.exists(dstPath.c_str());
  if (dstExists && !overwrite) {
    s.send(412, "text/plain", "Destination exists and Overwrite is F");
    return;
  }

  if (dstExists && !removeBookFile(dstPath.c_str())) {
    s.send(500, "text/plain", "Failed to remove destination");
    return;
  }

  HalFile file = Storage.open(srcPath.c_str());
  if (!file) {
    s.send(500, "text/plain", "Failed to open source");
    return;
  }

  clearBookCache(srcPath.c_str());
  file.close();
  const bool success = ClippingStore::moveBook(srcPath.c_str(), dstPath.c_str());

  if (success) {
    s.send(dstExists ? 204 : 201);
  } else {
    s.send(500, "text/plain", "Move failed");
  }
}

// ── COPY ─────────────────────────────────────────────────────────────────────

namespace {
bool stageDavCopy(HalFile& source, const char* temporary) {
  auto copy = Storage.openChecked(temporary, O_WRONLY | O_CREAT | O_EXCL);
  if (!copy) return false;

  uint8_t buffer[4096];
  uint64_t remaining = source.fileSize64();
  bool complete = !source.hasError();
  while (complete && remaining) {
    resetTaskWatchdogIfSubscribed();
    const size_t count = std::min<uint64_t>(remaining, sizeof(buffer));
    complete = source.read(buffer, count) == static_cast<int>(count) && !source.hasError() &&
               copy.write(buffer, count) == count;
    remaining -= count;
  }
  const bool sourceClosed = source.close();
  const bool synced = complete && sourceClosed && copy.sync() && !copy.hasError();
  const bool copyClosed = copy.close();
  if (synced && copyClosed) return true;

  Storage.remove(temporary);
  return false;
}

struct DavCopyPublication {
  const char* destination;
  const char* temporary;
  const char* backup;
  bool overwrite;
  int status = 500;
  bool discardTemporary = true;
};

bool publishDavCopy(void* context) {
  auto& copy = *static_cast<DavCopyPublication*>(context);
  using Status = HalStorage::PathStatus;
  const auto destination = Storage.lookupPath(copy.destination);
  if (destination == Status::Failed) return false;
  const bool exists = destination == Status::Present;
  if (exists && !copy.overwrite) {
    copy.status = 412;
    return false;
  }
  if (exists) {
    auto current = Storage.openChecked(copy.destination);
    if (!current) return false;
    const bool directory = current.isDirectory();
    if (!current.close()) return false;
    if (directory) {
      copy.status = 403;
      return false;
    }
    if (Storage.lookupPath(copy.backup) != Status::Missing) return false;
    copy.discardTemporary = false;
    if (!Storage.rename(copy.destination, copy.backup)) {
      LOG_ERR("DAV", "COPY could not park %s at %s", copy.destination, copy.backup);
      return false;
    }
  }

  // Preserve both candidates if publishing or restoring the old file is uncertain.
  copy.discardTemporary = false;
  if (!Storage.rename(copy.temporary, copy.destination)) {
    if (exists && Storage.lookupPath(copy.destination) == Status::Missing &&
        Storage.rename(copy.backup, copy.destination)) {
      copy.discardTemporary = true;
    } else {
      LOG_ERR("DAV", "COPY publication failed for %s, staged at %s, backup name %s", copy.destination, copy.temporary,
              copy.backup);
    }
    return false;
  }
  if (Storage.lookupPath(copy.destination) != Status::Present ||
      Storage.lookupPath(copy.temporary) != Status::Missing) {
    LOG_ERR("DAV", "COPY could not verify publication for %s", copy.destination);
    return false;
  }
  if (exists && !Storage.remove(copy.backup)) LOG_ERR("DAV", "COPY backup remains at %s", copy.backup);
  copy.status = exists ? 204 : 201;
  return true;
}
}  // namespace

void WebDAVHandler::handleCopy(WebServer& s) {
  String srcPath = getRequestPath(s);
  String dstPath = getDestinationPath(s);
  bool overwrite = getOverwrite(s);

  LOG_DBG("DAV", "COPY %s -> %s (overwrite=%d)", srcPath.c_str(), dstPath.c_str(), overwrite);

  if (isProtectedPath(srcPath) || protectedpaths::isSensitivePath(srcPath.c_str()) || isProtectedPath(dstPath) ||
      protectedpaths::isSensitivePath(dstPath.c_str(), protectedpaths::PathAccess::Write)) {
    s.send(403, "text/plain", "Forbidden");
    return;
  }

  if (dstPath.isEmpty()) {
    s.send(400, "text/plain", "Missing Destination header");
    return;
  }

  if (srcPath == dstPath) {
    s.send(204);
    return;
  }

  if (!Storage.exists(srcPath.c_str())) {
    s.send(404, "text/plain", "Source not found");
    return;
  }

  HalFile srcFile = Storage.openChecked(srcPath.c_str());
  if (!srcFile) {
    s.send(500, "text/plain", "Failed to open source");
    return;
  }

  if (srcFile.isDirectory()) {
    srcFile.close();
    s.send(403, "text/plain", "Cannot copy directories");
    return;
  }

  const size_t parentLength = std::max(1, dstPath.lastIndexOf('/'));
  const String parent = dstPath.substring(0, parentLength);
  if (parent.length() != parentLength || !Storage.exists(parent.c_str())) {
    s.send(409, "text/plain", "Destination parent unavailable");
    return;
  }
  if (!overwrite && Storage.exists(dstPath.c_str())) {
    s.send(412, "text/plain", "Destination exists and Overwrite is F");
    return;
  }

  char token[28];
  snprintf(token, sizeof(token), ".dav-copy-%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  const String prefix = parent == "/" ? parent : parent + "/";
  const String temporary = prefix + token + ".part";
  const String backup = prefix + token + ".bak";
  const size_t prefixLength = parentLength == 1 ? 1 : parentLength + 1;
  if (temporary.length() != prefixLength + strlen(token) + 5 || backup.length() != prefixLength + strlen(token) + 4 ||
      !stageDavCopy(srcFile, temporary.c_str())) {
    s.send(500, "text/plain", "Could not stage complete copy");
    return;
  }

  DavCopyPublication copy{dstPath.c_str(), temporary.c_str(), backup.c_str(), overwrite};
  if (Storage.withStorageLock(publishDavCopy, &copy)) {
    s.send(copy.status);
  } else {
    if (copy.discardTemporary) Storage.remove(temporary.c_str());
    s.send(copy.status, "text/plain", "Could not publish copy");
  }
}

// ── LOCK / UNLOCK (dummy for client compatibility) ───────────────────────────

void WebDAVHandler::handleLock(WebServer& s) {
  String path = getRequestPath(s);
  LOG_DBG("DAV", "LOCK %s (dummy)", path.c_str());

  // Return a dummy lock token for client compatibility
  String xml =
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<D:prop xmlns:D=\"DAV:\">\n"
      "<D:lockdiscovery><D:activelock>\n"
      "<D:locktype><D:write/></D:locktype>\n"
      "<D:lockscope><D:exclusive/></D:lockscope>\n"
      "<D:depth>infinity</D:depth>\n"
      "<D:owner><D:href>crosspoint</D:href></D:owner>\n"
      "<D:timeout>Second-3600</D:timeout>\n"
      "<D:locktoken><D:href>urn:uuid:dummy-lock-token</D:href></D:locktoken>\n"
      "<D:lockroot><D:href>/</D:href></D:lockroot>\n"
      "</D:activelock></D:lockdiscovery>\n"
      "</D:prop>\n";

  s.sendHeader("Lock-Token", "<urn:uuid:dummy-lock-token>");
  s.send(200, "application/xml; charset=\"utf-8\"", xml);
}

void WebDAVHandler::handleUnlock(WebServer& s) {
  LOG_DBG("DAV", "UNLOCK %s (dummy)", s.uri().c_str());
  s.send(204);
}

// ── Utility functions ────────────────────────────────────────────────────────

String WebDAVHandler::getRequestPath(WebServer& s) const {
  String uri = s.uri();
  String decoded = WebServer::urlDecode(uri);

  // Normalize using FsHelpers
  std::string normalized = FsHelpers::normalisePath(decoded.c_str());
  String result = normalized.c_str();

  if (result.isEmpty()) return "/";
  if (!result.startsWith("/")) result = "/" + result;

  // Remove trailing slash unless root
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }

  return result;
}

String WebDAVHandler::getDestinationPath(WebServer& s) const {
  String dest = s.header("Destination");
  if (dest.isEmpty()) return "";

  // Extract path from full URL: http://host/path -> /path
  // Find the third slash (after http://)
  int schemeEnd = dest.indexOf("://");
  if (schemeEnd >= 0) {
    int pathStart = dest.indexOf('/', schemeEnd + 3);
    if (pathStart >= 0) {
      dest = dest.substring(pathStart);
    } else {
      dest = "/";
    }
  }

  String decoded = WebServer::urlDecode(dest);
  std::string normalized = FsHelpers::normalisePath(decoded.c_str());
  String result = normalized.c_str();

  if (result.isEmpty()) return "/";
  if (!result.startsWith("/")) result = "/" + result;

  // Remove trailing slash unless root
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }

  return result;
}

void WebDAVHandler::urlEncodePath(const String& path, String& out) const {
  out = "";
  for (unsigned int i = 0; i < path.length(); i++) {
    char c = path.charAt(i);
    if (c == '/') {
      out += '/';
    } else if (c == ' ') {
      out += "%20";
    } else if (c == '%') {
      out += "%25";
    } else if (c == '#') {
      out += "%23";
    } else if (c == '?') {
      out += "%3F";
    } else if (c == '&') {
      out += "%26";
    } else if ((uint8_t)c > 127) {
      // Encode non-ASCII bytes
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (uint8_t)c);
      out += hex;
    } else {
      out += c;
    }
  }
}

bool WebDAVHandler::isProtectedPath(const String& path) const {
  // Check every segment of the path, not just the last one.
  // This prevents access to e.g. /.hidden/somefile or /System Volume Information/foo
  int start = 0;
  while (start < (int)path.length()) {
    if (path.charAt(start) == '/') {
      start++;
      continue;
    }
    int end = path.indexOf('/', start);
    if (end == -1) end = path.length();

    String segment = path.substring(start, end);

    if (segment.startsWith(".")) return true;

    for (const auto* item : FsHelpers::HIDDEN_ITEMS) {
      if (segment.equals(item)) return true;
    }

    start = end + 1;
  }

  return false;
}

int WebDAVHandler::getDepth(WebServer& s) const {
  String depth = s.header("Depth");
  if (depth == "0") return 0;
  if (depth == "1") return 1;
  // "infinity" or missing → treat as 1 (Class 1 servers don't need to support infinity)
  return 1;
}

bool WebDAVHandler::getOverwrite(WebServer& s) const {
  String ow = s.header("Overwrite");
  if (ow == "F" || ow == "f") return false;
  return true;  // Default is T
}

String WebDAVHandler::getMimeType(const String& path) {
  if (FsHelpers::hasEpubExtension(path)) return "application/epub+zip";
  if (FsHelpers::checkFileExtension(path, ".pdf")) return "application/pdf";
  if (FsHelpers::hasTxtExtension(path)) return "text/plain";
  if (FsHelpers::checkFileExtension(path, ".html") || FsHelpers::checkFileExtension(path, ".htm")) return "text/html";
  if (FsHelpers::checkFileExtension(path, ".css")) return "text/css";
  if (FsHelpers::checkFileExtension(path, ".js")) return "application/javascript";
  if (FsHelpers::checkFileExtension(path, ".json")) return "application/json";
  if (FsHelpers::checkFileExtension(path, ".xml")) return "application/xml";
  if (FsHelpers::hasJpgExtension(path)) return "image/jpeg";
  if (FsHelpers::hasPngExtension(path)) return "image/png";
  if (FsHelpers::hasGifExtension(path)) return "image/gif";
  if (FsHelpers::checkFileExtension(path, ".svg")) return "image/svg+xml";
  if (FsHelpers::checkFileExtension(path, ".zip")) return "application/zip";
  if (FsHelpers::checkFileExtension(path, ".gz")) return "application/gzip";
  return "application/octet-stream";
}
