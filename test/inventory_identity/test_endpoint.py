"""Exercise the actual read-only identity endpoint with an existing or missing secret."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('void CrossPointWebServer::handleInventoryIdentity() const {')
end = source.index('\n}\n', start) + 2
code = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
using String = std::string;
bool present=false; int calls=0;
namespace pluginidentity {
bool deviceId(const char* scope, size_t size, char (&out)[65], bool create) {
  assert(!create && std::string(scope,size)=="xteink-book-sync"); ++calls;
  if(!present) return false;
  std::memset(out,'a',64);out[64]=0;return true;
}}
struct Server {
 int status=0;std::string type,body;
 void send(int code,const char* mime,const String& data){status=code;type=mime;body=data;}
};
struct CrossPointWebServer {
 std::unique_ptr<Server> server=std::make_unique<Server>();
 void handleInventoryIdentity() const;
};
''' + source[start:end] + r'''
int main(){
 CrossPointWebServer web;
 web.handleInventoryIdentity();JsonDocument doc;assert(!deserializeJson(doc,web.server->body));
 assert(web.server->status==200 && web.server->type=="application/json" && doc["deviceId"].isNull());
 present=true;web.handleInventoryIdentity();doc.clear();assert(!deserializeJson(doc,web.server->body));
 assert(doc["deviceId"].as<std::string>()==std::string(64,'a') && calls==2);
}
'''
with tempfile.TemporaryDirectory() as temp:
    path = Path(temp)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', sys.argv[1], str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Inventory identity endpoint reports existing IDs without creating a secret')
