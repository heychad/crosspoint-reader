"""Exercise the guarded stage-hash handler with its real snapshot job."""
import ast
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def assignment(path, name):
    tree = ast.parse(path.read_text())
    return next(ast.literal_eval(node.value) for node in tree.body
                if isinstance(node, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == name for t in node.targets))


source = (ROOT / 'src/network/CrossPointWebServer.cpp').read_text()


def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == '{': depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0: return source[start:index + 1]
    raise AssertionError(signature)


storage = assignment(ROOT / 'test/stage_http/test_raw_session.py', 'STUB')
sha = assignment(ROOT / 'test/stage_http/test_stage_hash.py', 'sha')
code = r'''
#include <cassert>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include "CooperativeStageHash.h"
HalStorage Storage;
int allocations=0,failAt=-1,identityCalls=0,identityFailsAt=0;
template<typename T,typename... A>requires (!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(A&&... args){if(allocations++==failAt)return {};
 return std::make_unique<T>(std::forward<A>(args)...);}
template<typename T>requires std::is_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t n){if(allocations++==failAt)return {};return std::make_unique<T>(n);}
unsigned long ticks=1;unsigned long millis(){return ticks;}void yield(){}void resetTaskWatchdogIfSubscribed(){}
enum class SyncIdentityResult{Missing,Failed,Ready};
SyncIdentityResult readSyncIdentity(char* out){++identityCalls;if(identityCalls==identityFailsAt)return SyncIdentityResult::Failed;
 std::memset(out,'a',64);out[64]=0;return SyncIdentityResult::Ready;}
struct Client{bool alive=true;bool connected(){return alive;}};
struct Server{Client connection;std::unordered_map<std::string,std::string> args;int status=0;std::string body;
 bool hasArg(const char* n){return args.count(n);}std::string arg(const char* n){auto i=args.find(n);return i==args.end()?"":i->second;}
 Client& client(){return connection;}void send(int s,const char*,const char* b){status=s;body=b;}};
struct CrossPointWebServer {struct SyncHashState;bool running=true;std::unique_ptr<Server> server=std::make_unique<Server>();
 std::unique_ptr<SyncHashState> syncHash;std::function<bool()> uploadCancelCheck;
 CrossPointWebServer();~CrossPointWebServer();CrossPointWebServer(CrossPointWebServer&&) noexcept;
 void handleSyncStageHash();void handleSyncStageHashCancel();void stepSyncStageHash();};
''' + source[source.index('struct CrossPointWebServer::SyncHashState {'):source.index('\n#endif', source.index('struct CrossPointWebServer::SyncHashState {'))] + r'''
CrossPointWebServer::CrossPointWebServer()=default;CrossPointWebServer::~CrossPointWebServer()=default;
CrossPointWebServer::CrossPointWebServer(CrossPointWebServer&&) noexcept=default;
''' + function('bool parseSyncSize(') + '\n' + function('void CrossPointWebServer::handleSyncStageHash()') + '\n' + function('void CrossPointWebServer::stepSyncStageHash()') + '\n' + function('void CrossPointWebServer::handleSyncStageHashCancel()') + r'''
int main(){
 const std::string name="__xsync_0123456789abcdef0123456789abcdef.part";
 auto prepare=[&](){CrossPointWebServer s;s.server->args={{"name",name},{"size","3"},{"deviceId",std::string(64,'a')}};
  allocations=0;failAt=-1;identityCalls=0;identityFailsAt=0;Storage.exists=true;Storage.bytes={'a','b','c'};return s;};
 {auto s=prepare();s.handleSyncStageHash();assert(s.server->status==202);s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==200);
  std::string expected="{\"sha256\":\"";for(int i=0;i<32;++i)expected+="26";expected+="\"}";
  assert(s.server->body==expected && identityCalls==2);}
 {auto s=prepare();s.server->args["size"]="03";s.handleSyncStageHash();assert(s.server->status==400);}
 {auto s=prepare();s.server->args["name"]="../book";s.handleSyncStageHash();assert(s.server->status==400);}
 {auto s=prepare();s.server->args["deviceId"]=std::string(64,'b');s.handleSyncStageHash();assert(s.server->status==409);}
 {auto s=prepare();identityFailsAt=2;s.handleSyncStageHash();s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==409);}
 {auto s=prepare();s.uploadCancelCheck=[] {return true;};s.handleSyncStageHash();assert(s.server->status==409);}
 {auto s=prepare();Storage.readOk=false;s.handleSyncStageHash();s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==409);Storage.readOk=true;}
 {auto s=prepare();failAt=0;s.handleSyncStageHash();assert(s.server->status==503);}
 {auto s=prepare();s.handleSyncStageHash();assert(s.server->status==202);s.server->args["size"]="2";
  s.handleSyncStageHash();assert(s.server->status==409);s.server->args["size"]="3";
  s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==200);}
 {auto s=prepare();Storage.bytes.assign(9000,'x');s.server->args["size"]="9000";
  s.handleSyncStageHash();assert(s.server->status==202);s.handleSyncStageHash();assert(s.server->status==202);
  s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==202);
  s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==202);
  s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==200);}
 {auto s=prepare();s.handleSyncStageHash();s.uploadCancelCheck=[] {return true;};
  s.stepSyncStageHash();s.handleSyncStageHash();assert(s.server->status==409);}
 {auto s=prepare();s.handleSyncStageHash();ticks+=3600000;s.stepSyncStageHash();
  s.handleSyncStageHash();assert(s.server->status==409);ticks=1;}
 {auto s=prepare();s.handleSyncStageHash();assert(s.server->status==202);s.server->args["size"]="2";
  s.handleSyncStageHashCancel();assert(s.server->status==404 && s.syncHash);
  s.server->args["size"]="3";s.handleSyncStageHashCancel();assert(s.server->status==200 && !s.syncHash);
  s.handleSyncStageHashCancel();assert(s.server->status==404);}
 {auto s=prepare();s.handleSyncStageHash();Storage.readOk=false;s.stepSyncStageHash();assert(s.syncHash);
  ticks+=30000;s.stepSyncStageHash();assert(!s.syncHash);Storage.readOk=true;ticks=1;}
 {auto s=prepare();s.handleSyncStageHash();Storage.readOk=false;s.stepSyncStageHash();Storage.readOk=true;
  s.server->args["size"]="4";s.handleSyncStageHash();assert(s.server->status==409 && s.syncHash);
  assert(s.server->body=="{\"error\":\"verification busy\"}");s.server->args["size"]="3";
  s.handleSyncStageHash();assert(s.server->status==409 && !s.syncHash);
  assert(s.server->body=="{\"error\":\"verification failed\"}");}
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(storage)
    (directory / 'mbedtls').mkdir()
    (directory / 'mbedtls/sha256.h').write_text(sha)
    (directory / 'test.cpp').write_text(code)
    executable = directory / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'test.cpp'), '-o', str(executable)],
                   check=True, timeout=30)
    subprocess.run([str(executable)], check=True, timeout=10)
print('Guarded stage-hash HTTP handler checks passed with a digest stub')
