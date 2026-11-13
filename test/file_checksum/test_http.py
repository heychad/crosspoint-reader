"""Run the checksum HTTP handlers with the production job and a fake transport."""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('void CrossPointWebServer::sendFileChecksum()')
end = source.index('\n#endif', start)
program = r'''
#include <ArduinoJson.h>
#include <Memory.h>
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include "network/FileChecksum.h"
using String = std::string;
HalStorage Storage;
uint32_t now = 0;
unsigned long millis() { return now; }
void resetTaskWatchdogIfSubscribed() {}
void yield() {}
void esp_fill_random(void* raw, size_t count) {
  static uint8_t seed = 0;
  auto* bytes = static_cast<uint8_t*>(raw);
  for (size_t i = 0; i < count; ++i) bytes[i] = ++seed;
}
String normalizeWebPath(const String& path) { return path; }
bool wsUploadInProgress = false;
struct Server {
  std::map<String, String> params, headers;
  int status = 0;
  String body;
  String arg(const char* key) { auto it = params.find(key); return it == params.end() ? "" : it->second; }
  bool hasArg(const char* key) { return params.count(key); }
  int args() { return params.size(); }
  void sendHeader(const char* key, const char* value) { headers[key] = value; }
  void send(int code, const char*, const String& response) { status = code; body = response; }
};
struct CrossPointWebServer {
  std::unique_ptr<Server> server = std::make_unique<Server>();
  std::unique_ptr<filechecksum::Job> fileChecksum;
  std::function<bool()> uploadCancelCheck;
  char fileChecksumToken[33]{};
  uint32_t fileChecksumLastRequest = 0;
  struct { HalFile file; } upload;
  void handleFileChecksumStart(); void handleFileChecksumStatus(); void handleFileChecksumCancel();
  void sendFileChecksum(); void advanceFileChecksum();
};
''' + source[start:end] + r'''
constexpr char PATH[] = "/Authors/Café.epub";
JsonDocument json(CrossPointWebServer& server) {
  JsonDocument doc;
  assert(!deserializeJson(doc, server.server->body));
  return doc;
}
void reset(size_t bytes = 3) {
  fake::files.clear(); fake::epoch = fake::writers = 0;
  fake::failRead = fake::failClose = failAllocation = wsUploadInProgress = false;
  auto file = std::make_shared<fake::File>(); file->bytes = {'a','b','c'};
  file->bytes.resize(bytes); fake::files[PATH] = file; now = 0;
}
void start(CrossPointWebServer& server, size_t bytes = 3) {
  server.server->params = {{"path", PATH}, {"size", std::to_string(bytes)}};
  server.handleFileChecksumStart();
}
void poll(CrossPointWebServer& server) {
  server.server->params = {{"token", server.fileChecksumToken}};
  server.handleFileChecksumStatus();
}
int main() {
  reset();
  {
    CrossPointWebServer s; start(s);
    auto response = json(s);
    assert(s.server->status == 202 && response["state"] == "pending" && response["path"] == PATH);
    assert(response["size"] == 3 && response["processed"] == 0 && !response["sha256"].is<const char*>());
    assert(response["token"].as<String>().size() == 32 && s.server->headers["Cache-Control"] == "no-store");
    start(s); assert(s.server->status == 409 && json(s)["state"] == "busy");
    poll(s); assert(s.server->status == 202);
    s.advanceFileChecksum(); poll(s); response = json(s);
    assert(s.server->status == 200 && response["state"] == "complete" && response["processed"] == 3);
    assert(response["sha256"] == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    ++fake::epoch; poll(s);
    assert(s.server->status == 409 && json(s)["state"] == "changed" && !json(s)["sha256"].is<const char*>());
  }
  reset(filechecksum::SLICE_BYTES + 3);
  {
    CrossPointWebServer s; start(s, filechecksum::SLICE_BYTES + 3); s.advanceFileChecksum(); poll(s);
    assert(s.server->status == 202 && json(s)["processed"] == filechecksum::SLICE_BYTES);
    const String token = s.fileChecksumToken;
    s.server->params = {{"token", String(32, 'f')}}; s.handleFileChecksumCancel();
    assert(s.server->status == 404 && s.fileChecksum->state() == filechecksum::State::Pending);
    s.server->params = {{"token", token}}; s.handleFileChecksumCancel();
    assert(s.server->status == 200 && json(s)["state"] == "cancelled");
    s.handleFileChecksumCancel(); assert(s.server->status == 200);
    assert(fake::files.at(PATH).use_count() == 1 && fake::epoch == 0);
  }
  reset();
  {
    CrossPointWebServer s; start(s); s.uploadCancelCheck = [] { return true; }; s.advanceFileChecksum(); poll(s);
    assert(s.server->status == 200 && json(s)["state"] == "cancelled");
    now = 60000; s.advanceFileChecksum(); assert(!s.fileChecksum);
    poll(s); assert(s.server->status == 404);
  }
  reset();
  {
    CrossPointWebServer s; now = UINT32_MAX - 20; start(s); now = 10; s.advanceFileChecksum(); poll(s);
    assert(s.server->status == 200 && json(s)["state"] == "complete");
    now += 60000; poll(s); assert(s.server->status == 404);
    s.advanceFileChecksum(); assert(!s.fileChecksum);
  }
  reset();
  for (const char* path : {"/", "/.crosspoint/wifi.json", "/CROSSP~1/WIFI.JSO", "/Books/../x.epub", "/a//b.epub"}) {
    CrossPointWebServer s; s.server->params = {{"path",path},{"size","3"}}; s.handleFileChecksumStart();
    assert(s.server->status == 400 && !s.fileChecksum);
  }
  {
    CrossPointWebServer s; s.server->params = {{"path",String("/a\0.epub",8)},{"size","3"}};
    s.handleFileChecksumStart(); assert(s.server->status == 400 && !s.fileChecksum);
    s.server->params = {{"path",PATH},{"size",String("3\0",2)}};
    s.handleFileChecksumStart(); assert(s.server->status == 400);
    s.server->params = {{"path",PATH},{"size","3"},{"extra","1"}};
    s.handleFileChecksumStart(); assert(s.server->status == 400);
    s.server->params.erase("size"); s.handleFileChecksumStart(); assert(s.server->status == 400);
    start(s,4); assert(s.server->status == 409 && json(s)["state"] == "wrong_size");
    s.server->params = {{"path","/missing.epub"},{"size","3"}}; s.handleFileChecksumStart();
    assert(s.server->status == 404 && json(s)["state"] == "not_found");
  }
  reset();
  {
    CrossPointWebServer s; wsUploadInProgress = true; start(s); assert(s.server->status == 409);
    wsUploadInProgress = false; failAllocation = true; start(s); assert(s.server->status == 503);
    failAllocation = false; start(s); fake::failRead = true; s.advanceFileChecksum(); poll(s);
    assert(s.server->status == 503 && json(s)["state"] == "io_error" && !json(s)["sha256"].is<const char*>());
  }
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    src, binary = directory / 'test.cpp', directory / 'test'
    src.write_text(program)
    json_include = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-DHEX=16', '-Wno-deprecated-declarations',
               *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', str(json_include),
               '-I', str(ROOT / 'test/sync_staging/stubs'), '-I', str(ROOT / 'src'),
               '-I', str(ROOT / 'lib/hal'), '-I', str(ROOT / 'lib/FsHelpers'),
               str(src), str(ROOT / 'src/network/FileChecksum.cpp'), '-o', str(binary)]
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True, timeout=20)
print('Reader checksum HTTP integration checks passed')
