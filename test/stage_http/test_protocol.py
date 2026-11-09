"""Exercise staging upload handlers with controlled NVS, network and storage."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
# Keep the upload route unreachable until the framework bounds multipart parsing.
assert 'server->on("/api/sync/stage"' not in source
assert 'server->handleClient();\n  // Multipart parsing can fail' in source
state_start = source.index('struct CrossPointWebServer::SyncStageUpload {')
namespace_start = source.index('namespace {', state_start)
identity_start = source.index('enum class SyncIdentityResult', namespace_start)
network_start = source.index('using InventoryActive', identity_start)
methods_start = source.index('void CrossPointWebServer::handleSyncIdentity()', network_start)
methods_end = source.index('// File listing page template', methods_start)
handlers = (source[state_start:namespace_start] + 'namespace {\nconstexpr unsigned long INVENTORY_IDLE_MS = 60000;\n' +
            source[identity_start:network_start] + '}\n' + source[methods_start:methods_end])
code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
using esp_err_t=int;using nvs_handle_t=int;
constexpr int ESP_OK=0,ESP_ERR_NVS_NOT_FOUND=1,NVS_READONLY=2;
int nvsMode=0;std::string hashedInput;
int nvs_open(const char* name,int mode,nvs_handle_t* h){assert(std::strcmp(name,"devid")==0 && mode==NVS_READONLY);*h=1;return nvsMode==1?ESP_ERR_NVS_NOT_FOUND:nvsMode==2?3:ESP_OK;}
int nvs_get_blob(nvs_handle_t,const char* key,void* output,size_t* size){
 assert(std::strcmp(key,"secret")==0);
 if(nvsMode==3)return ESP_ERR_NVS_NOT_FOUND;if(nvsMode==4)return 3;
 std::memset(output,0x11,*size);if(nvsMode==5)*size=3;return ESP_OK;}
void nvs_close(nvs_handle_t){}
int wc_Sha256Hash(const uint8_t* input,size_t size,uint8_t* out){hashedInput.assign(reinterpret_cast<const char*>(input),size);
 for(unsigned i=0;i<32;++i)out[i]=static_cast<uint8_t>(i);return 0;}
unsigned long now=10;unsigned long millis(){return now;}void yield(){}void resetTaskWatchdogIfSubscribed(){}
int allocCount=0,failAt=-1;
template<typename T,typename... A>requires (!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(A&&... a){if(allocCount++==failAt)return {};return std::make_unique<T>(std::forward<A>(a)...);}
template<typename T>requires std::is_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t n){if(allocCount++==failAt)return {};return std::make_unique<T>(n);}
struct StorageType{} Storage;
bool beginOk=true,appendOk=true,finishOk=true;int begins=0;std::vector<uint8_t> written;
namespace wirecheck {
class ExclusiveStage {
 public:
 explicit ExclusiveStage(StorageType&){}
 static bool validName(const char* n){return n && std::strlen(n)==45 && std::strncmp(n,"__xsync_",8)==0 && std::strcmp(n+40,".part")==0;}
 bool begin(const char*,uint64_t){++begins;return beginOk;}
 bool append(const uint8_t* p,size_t n){if(!appendOk)return false;written.insert(written.end(),p,p+n);return true;}
 bool finish(){return finishOk;}
};
}
enum HTTPUploadStatus{UPLOAD_FILE_START,UPLOAD_FILE_WRITE,UPLOAD_FILE_END,UPLOAD_FILE_ABORTED};
struct HTTPUpload{HTTPUploadStatus status=UPLOAD_FILE_START;std::string filename;size_t currentSize=0;uint8_t buf[20000]{};};
struct Client{bool alive=true;bool connected()const{return alive;}void stop(){alive=false;}};
struct Server {
 Client connection;HTTPUpload current;std::unordered_map<std::string,std::string> args;int status=0;std::string body;
 Client& client(){return connection;}HTTPUpload& upload(){return current;}
 bool hasArg(const char* key){return args.count(key);}std::string arg(const char* key){auto it=args.find(key);return it==args.end()?"":it->second;}
 void send(int code,const char*,const char* bodyValue){status=code;body=bodyValue;}
};
class CrossPointWebServer {
 public:
 struct SyncStageUpload;std::unique_ptr<SyncStageUpload> syncStage;bool syncStageSeen=false;unsigned long syncStageLastActivity=0;
 std::unique_ptr<Server> server=std::make_unique<Server>();bool running=true;
 std::function<bool()> cancel;
 CrossPointWebServer();~CrossPointWebServer();
 bool dropUploadIfCancelled()const{if(!cancel || !cancel())return false;server->client().stop();return true;}
 void handleSyncIdentity()const;void handleSyncStageData();void handleSyncStagePost();void expireSyncStage();void clearSyncStageRequest();
};
''' + handlers + r'''
CrossPointWebServer::CrossPointWebServer()=default;CrossPointWebServer::~CrossPointWebServer()=default;
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];CrossPointWebServer s;
 if(mode=="missing"){nvsMode=1;s.handleSyncIdentity();assert(s.server->status==200 && s.server->body=="{\"deviceId\":null}");return 0;}
 if(mode=="nvs-error"){nvsMode=4;s.handleSyncIdentity();assert(s.server->status==503);return 0;}
 s.handleSyncIdentity();assert(s.server->status==200 && hashedInput.size()==48 && hashedInput.substr(32)=="xteink-book-sync");
 const std::string id="000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";
 assert(s.server->body=="{\"deviceId\":\""+id+"\"}");
 s.server->args={{"deviceId",(mode=="wrong-id" || mode=="cancel-rejected")?"bad":id},{"path","/"},{"size",mode=="short"?"20001":"20000"}};
 auto& upload=s.server->current;upload.filename="__xsync_0123456789abcdef0123456789abcdef.part";
 if(mode=="invalid-size")s.server->args["size"]="0005";
 if(mode=="nul-id")s.server->args["deviceId"]=id+std::string("\0extra",6);
 if(mode=="nul-size")s.server->args["size"]=std::string("20000\0extra",11);
 if(mode=="nul-name")upload.filename+=std::string("\0extra",6);
 if(mode=="oversized")s.server->args["size"]="19999";
 if(mode=="begin-fail")beginOk=false;
 if(mode=="oom")failAt=1;
 if(mode=="oom-state")failAt=0;
 s.handleSyncStageData();
 if(mode=="cancel-rejected"){assert(begins==0);s.cancel=[] {return true;};upload.status=UPLOAD_FILE_WRITE;
  upload.currentSize=1;s.handleSyncStageData();assert(!s.server->connection.alive);return 0;}
 if(mode=="oom-state"){assert(s.syncStageSeen && !s.syncStage);now+=60001;s.expireSyncStage();
  assert(!s.syncStageSeen);return 0;}
 if(mode=="wrong-id" || mode=="invalid-size" || mode=="nul-id" || mode=="nul-size" || mode=="nul-name" || mode=="oom"){
  assert(begins==0);s.handleSyncStagePost();assert(s.server->status==400);return 0;
 }
 assert(begins==1);
 if(mode=="begin-fail"){s.handleSyncStagePost();assert(s.server->status==400);return 0;}
 if(mode=="duplicate"){s.handleSyncStageData();assert(begins==1);s.handleSyncStagePost();assert(s.server->status==400);return 0;}
 if(mode=="abort"){upload.status=UPLOAD_FILE_ABORTED;s.handleSyncStageData();assert(!s.syncStage && !s.syncStageSeen);return 0;}
 if(mode=="expiry"){now+=60001;s.expireSyncStage();assert(!s.syncStage && !s.syncStageSeen);return 0;}
 if(mode=="cancel")s.cancel=[] {return true;};
 if(mode=="write-fail")appendOk=false;
 upload.status=UPLOAD_FILE_WRITE;upload.currentSize=10000;std::memset(upload.buf,0x5a,10000);s.handleSyncStageData();
 if(mode=="cancel"){s.handleSyncStagePost();assert(s.server->status==400 && begins==1);return 0;}
 s.handleSyncStageData();
 if(mode=="identity-change")nvsMode=4;
 if(mode=="finish-fail")finishOk=false;
 if(mode=="disconnected")s.server->connection.alive=false;
 upload.status=UPLOAD_FILE_END;s.handleSyncStageData();
 if(mode=="parser-failure"){s.clearSyncStageRequest();s.handleSyncStagePost();assert(s.server->status==400);return 0;}
 s.handleSyncStagePost();
 if(mode=="short" || mode=="identity-change" || mode=="oversized" || mode=="write-fail" ||
    mode=="finish-fail" || mode=="disconnected")assert(s.server->status==400);
 else {assert(s.server->status==200 && s.server->body=="{\"staged\":true}");assert(written.size()==20000);}
}
'''

with tempfile.TemporaryDirectory() as tmp:
    cpp, executable = Path(tmp) / 'stage.cpp', Path(tmp) / 'stage-test'
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(cpp), '-o', str(executable)], check=True)
    for mode in ('missing', 'nvs-error', 'success', 'wrong-id', 'invalid-size', 'nul-id', 'nul-size', 'nul-name',
                 'oom', 'oom-state', 'begin-fail', 'duplicate', 'abort', 'expiry', 'cancel', 'cancel-rejected', 'parser-failure', 'short',
                 'oversized', 'write-fail', 'finish-fail', 'disconnected', 'identity-change'):
        subprocess.run([str(executable), mode], check=True)
print('Staging HTTP identity, buffering, cancellation and failure checks passed')
