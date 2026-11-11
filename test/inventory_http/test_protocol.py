"""Exercise actual inventory HTTP handlers with a fault-injecting network."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('struct CrossPointWebServer::InventoryWebState {')
end = source.index('\n#endif', start)
handlers = source[start:end]
code = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#define HEX 16
#define MSG_DONTWAIT 0x40
unsigned long now=10;
unsigned long millis(){return now;}
void yield(){} void delay(unsigned long ms){now+=ms;} void resetTaskWatchdogIfSubscribed(){}
void esp_fill_random(void* p,size_t n){for(size_t i=0;i<n;++i)static_cast<uint8_t*>(p)[i]=static_cast<uint8_t>(i);}
template<typename T,typename... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){return std::make_unique<T>(std::forward<A>(a)...);}
struct NetworkClient {
 bool alive=true;size_t limit=SIZE_MAX,maxWrite=SIZE_MAX;std::string data;
 bool connected()const{return alive;}
 int fd()const{return 7;}
 size_t write(const uint8_t* p,size_t n){if(data.size()>=limit)return 0;n=std::min(n,std::min(maxWrite,limit-data.size()));data.append(reinterpret_cast<const char*>(p),n);return n;}
 void stop(){alive=false;}
};
NetworkClient* currentClient=nullptr;
int send(int fd,const void* bytes,size_t n,int flags){
 assert(fd==7 && flags==MSG_DONTWAIT && currentClient);
 const size_t sent=currentClient->write(static_cast<const uint8_t*>(bytes),n);
 if(!sent){errno=EAGAIN;return -1;}return static_cast<int>(sent);
}
struct Server {
 NetworkClient connection;std::unordered_map<std::string,std::string> args;int status=0;std::string body;
 NetworkClient& client(){currentClient=&connection;return connection;}
 bool hasArg(const char* key){return args.count(key);}
 std::string arg(const char* key){return args.at(key);}
 void send(int code,const char*,const char* text){status=code;body=text;}
};
struct StorageType{} Storage;
bool snapshotOk=true,beginOk=true;
namespace wirecheck {
enum class ScanResult {Pending,Complete,Failed};
struct DirectoryEntry {char name[766]{};uint64_t size=0;bool directory=false;};
class InventorySession {
 public:
 struct Callbacks {void* context;bool(*keepGoing)(void*);bool(*emit)(void*,const DirectoryEntry&);};
 Callbacks callbacks;int index=0;
 InventorySession(StorageType&,Callbacks cb):callbacks(cb){}
 bool snapshotValid(){return snapshotOk;}
 bool beginDirectory(const char*){index=0;return beginOk;}
 static bool includedName(const char* n){return n && *n && *n!='.' && std::strcmp(n,"XTCache") && std::strcmp(n,"System Volume Information");}
 ScanResult step(){if(!callbacks.keepGoing(callbacks.context))return ScanResult::Failed;
  if(index++==0){DirectoryEntry e;std::strcpy(e.name,"A book.epub");e.size=42;
   return callbacks.emit(callbacks.context,e)?ScanResult::Pending:ScanResult::Failed;}
  if(index==2){DirectoryEntry e;std::strcpy(e.name,".crosspoint");e.directory=true;
   return callbacks.emit(callbacks.context,e)?ScanResult::Pending:ScanResult::Failed;}
  return ScanResult::Complete;}
};
}
class CrossPointWebServer {
 public:
 struct InventoryWebState;
 std::unique_ptr<Server> server=std::make_unique<Server>();
 std::unique_ptr<InventoryWebState> inventory;
 bool running=true;std::function<bool()> uploadCancelCheck;
 CrossPointWebServer();~CrossPointWebServer();
 void handleInventoryStart();void handleInventoryDirectory();void handleInventoryFinish();
 void expireInventory();bool inventoryActive()const;bool inventoryEmit(const wirecheck::DirectoryEntry&);
};
''' + handlers + r'''
CrossPointWebServer::CrossPointWebServer()=default;
CrossPointWebServer::~CrossPointWebServer()=default;
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];CrossPointWebServer s;s.handleInventoryStart();
 assert(s.server->status==200 && s.inventory && std::strlen(s.inventory->token)==32);
 std::string token=s.inventory->token;
 if(mode=="busy"){s.handleInventoryStart();assert(s.server->status==409 && s.inventory);return 0;}
 if(mode=="expiry"){now+=60001;s.expireInventory();assert(!s.inventory);return 0;}
 s.server->args={{"token",mode=="wrong-token"?"bad":token},{"path","/Books"}};
 if(mode=="wrong-token"){s.handleInventoryDirectory();assert(s.server->status==404 && s.inventory);return 0;}
 if(mode=="invalid"){beginOk=false;s.handleInventoryDirectory();assert(s.server->status==409 && !s.inventory);return 0;}
 if(mode=="partial")s.server->connection.maxWrite=3;
 const bool cut=mode.rfind("cut-",0)==0;
 if(cut)s.server->connection.limit=std::stoul(mode.substr(4));
 if(mode=="disconnect" || mode=="timeout" || mode=="cancel-later")s.server->connection.limit=150;
 if(mode=="cancel")s.uploadCancelCheck=[] {return true;};
 if(mode=="cancel-later")s.uploadCancelCheck=[] {return now>=60;};
 s.handleInventoryDirectory();
 if(mode=="disconnect" || mode=="cancel" || mode=="timeout" || mode=="cancel-later" || cut){
  assert(!s.inventory && !s.server->connection.alive);
  if(cut) {
   const auto& data=s.server->connection.data;
   assert(data.size()<5 || data.substr(data.size()-5)!="0\r\n\r\n");
  } else assert(s.server->connection.data.find("complete\":true")==std::string::npos);
  if(mode=="timeout")assert(now>=2010 && now<2030);
  if(mode=="cancel-later")assert(now>=60 && now<2010);return 0;
 }
 assert(s.inventory && !s.server->connection.alive);
 if(mode=="stale")snapshotOk=false;
 s.server->connection.alive=true;s.server->args={{"token",token}};s.handleInventoryFinish();
 assert(s.server->status==(mode=="stale"?409:200) && !s.inventory);
 assert(s.server->body==(mode=="stale"?"{\"snapshotValid\":false}":"{\"snapshotValid\":true}"));
 if(mode=="normal" || mode=="partial")std::fwrite(s.server->connection.data.data(),1,s.server->connection.data.size(),stdout);
}
'''


def parse_http(raw):
    header, body = raw.split(b'\r\n\r\n', 1)
    assert header.startswith(b'HTTP/1.1 200 OK\r\n') and b'Transfer-Encoding: chunked' in header
    assert b'Access-Control-Allow-Origin: *' in header
    chunks = []
    while True:
        line, body = body.split(b'\r\n', 1)
        size = int(line, 16)
        if not size:
            assert body == b'\r\n'
            break
        chunks.append(body[:size])
        assert body[size:size + 2] == b'\r\n'
        body = body[size + 2:]
    return json.loads(b''.join(chunks))


with tempfile.TemporaryDirectory() as tmp:
    cpp, executable = Path(tmp) / 'http.cpp', Path(tmp) / 'http-test'
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(cpp), '-o', str(executable)], check=True)
    complete = subprocess.run([str(executable), 'normal'], capture_output=True, check=True).stdout
    for cut in range(len(complete)):
        subprocess.run([str(executable), f'cut-{cut}'], capture_output=True, check=True)
    for mode in ('normal', 'partial', 'disconnect', 'timeout', 'cancel', 'cancel-later', 'stale', 'invalid', 'wrong-token', 'busy', 'expiry'):
        result = subprocess.run([str(executable), mode], capture_output=True, check=True)
        if mode in ('normal', 'partial'):
            assert parse_http(result.stdout) == {'entries': [{'name': 'A book.epub', 'size': 42,
                                                              'isDirectory': False}], 'complete': True}
print('Inventory HTTP sessions, partial writes, cancellation, and stale snapshots passed')
