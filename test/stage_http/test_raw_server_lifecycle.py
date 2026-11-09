"""Exercise the extracted guarded listener owner with a simulated web server."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()
start = source.index('struct CrossPointWebServer::SyncRawState {')
end = source.index('\n};', start) + 3
state = source[start:end]


def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for pos in range(opening, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if not depth:
            return source[start:pos + 1]
    raise AssertionError(signature)


methods = '\n'.join(function(x) for x in ('void CrossPointWebServer::startSyncRaw()',
                                           'void CrossPointWebServer::stopSyncRaw()'))
code = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <string>
#define LOG_ERR(...) ((void)0)
int Storage=0;uint32_t millis(){return 123;}
enum class SyncIdentityResult{Ready,Missing,Failed};
SyncIdentityResult identityResult=SyncIdentityResult::Ready;
SyncIdentityResult readSyncIdentity(char (&out)[65]){std::memset(out,'a',64);out[64]=0;return identityResult;}
namespace wirecheck {
struct GuardedRawStageListener {
 struct Callbacks {void* context;uint32_t (*now)(void*);bool (*active)(void*);bool (*identity)(void*,const char*);};
 inline static int alive=0;inline static bool beginSucceeds=true;
 Callbacks callbacks;uint16_t port=0;
 GuardedRawStageListener(int&,uint64_t reserve,Callbacks c,const std::atomic_flag&):callbacks(c){assert(reserve==64ULL*1024*1024);++alive;}
 ~GuardedRawStageListener(){--alive;}
 bool begin(uint16_t p){port=p;return beginSucceeds;}
};
}
class CrossPointWebServer {
 public:
 struct SyncRawState;
 std::unique_ptr<SyncRawState> syncRaw;
 bool running=false,experimentalSyncUploadEnabled=false;
 void* server=nullptr;std::function<bool()> uploadCancelCheck;
 void startSyncRaw();void stopSyncRaw();~CrossPointWebServer();
};
''' + state + '\n' + methods + r'''
CrossPointWebServer::~CrossPointWebServer(){stopSyncRaw();}
int main(){
 CrossPointWebServer s;s.startSyncRaw();assert(!s.syncRaw);
 s.running=true;s.server=&s;s.startSyncRaw();assert(!s.syncRaw);
 s.experimentalSyncUploadEnabled=true;wirecheck::GuardedRawStageListener::beginSucceeds=false;
 s.startSyncRaw();assert(!s.syncRaw && wirecheck::GuardedRawStageListener::alive==0);
 wirecheck::GuardedRawStageListener::beginSucceeds=true;s.startSyncRaw();
 assert(s.syncRaw && s.syncRaw->listener.port==8182 && wirecheck::GuardedRawStageListener::alive==1);
 auto cb=s.syncRaw->listener.callbacks;assert(cb.now(cb.context)==123 && cb.active(cb.context));
 const std::string id(64,'a');assert(cb.identity(cb.context,id.c_str()));
 assert(!cb.identity(cb.context,std::string(64,'b').c_str()));
 identityResult=SyncIdentityResult::Missing;assert(!cb.identity(cb.context,id.c_str()));
 s.uploadCancelCheck=[](){return true;};assert(!cb.active(cb.context));
 s.startSyncRaw();assert(wirecheck::GuardedRawStageListener::alive==1);
 s.stopSyncRaw();assert(!s.syncRaw && wirecheck::GuardedRawStageListener::alive==0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'lifecycle.cpp'
    binary = Path(tmp) / 'lifecycle'
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(cpp), '-o', str(binary)], check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=10)
print('Experimental guarded listener owner lifecycle passed')
