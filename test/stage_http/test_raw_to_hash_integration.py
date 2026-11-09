"""Stage bytes via the C++ listener, then verify via the actual C++ handler."""
import hashlib
import io
import os
from pathlib import Path
import runpy
import select
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts'))
from wireless_hash_poll import verify_stage
from wireless_raw_stage import upload_stage

fixture = runpy.run_path(str(ROOT / 'test/stage_http/test_hash_poll_integration.py'))
try:
    crypto_flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libcrypto'],
                                                      text=True, stderr=subprocess.DEVNULL))
except (OSError, subprocess.CalledProcessError):
    print('Host OpenSSL is unavailable; staged hash integration skipped')
    raise SystemExit(77)

prefix = fixture['prefix']
code = prefix + r'''
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <thread>
#include "RawStageListener.h"
struct RawContext{std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();};
uint32_t now(void* p){auto& c=*static_cast<RawContext*>(p);return std::chrono::duration_cast<std::chrono::milliseconds>(
 std::chrono::steady_clock::now()-c.start).count();}
bool active(void*){return true;}
bool verify(void*,const char* id){return std::string(id)==std::string(64,'a');}
int main(){
 RawContext context;wirecheck::RawStageListener raw(Storage,{&context,now,active,verify});assert(raw.begin(0));
 int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in address{};
 address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
 assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 && listen(listener,1)==0);
 const int flags=fcntl(listener,F_GETFL,0);assert(flags>=0 && fcntl(listener,F_SETFL,flags|O_NONBLOCK)==0);
 socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
 std::printf("%u %u\n",raw.localPort(),ntohs(address.sin_port));std::fflush(stdout);
 CrossPointWebServer service;const std::string name="__xsync_0123456789abcdef0123456789abcdef.part";
 service.server->args={{"name",name},{"size","9000"},{"deviceId",std::string(64,'a')}};
 for(int loops=0;loops<30000;++loops){
  raw.poll();const int client=accept(listener,nullptr,nullptr);
  if(client<0){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
  const int clientFlags=fcntl(client,F_GETFL,0);assert(clientFlags>=0 && fcntl(client,F_SETFL,clientFlags&~O_NONBLOCK)==0);
  char request[1024]{};std::string line;
  while(line.find("\r\n\r\n")==std::string::npos){const auto n=recv(client,request,sizeof(request),0);
   assert(n>0);line.append(request,n);assert(line.size()<4096);}
  const std::string expected="GET /api/sync/stage-hash?name="+name+"&size=9000&deviceId="+std::string(64,'a')+" HTTP/1.1";
  assert(line.starts_with(expected));service.handleSyncStageHash();const int status=service.server->status;
  const std::string body=service.server->body;
  const std::string response="HTTP/1.1 "+std::to_string(status)+" OK\r\nContent-Length: "+
      std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
  size_t sent=0;while(sent<response.size()){const auto n=send(client,response.data()+sent,response.size()-sent,0);
   assert(n>0);sent+=static_cast<size_t>(n);}close(client);
  if(status==200){assert(raw.completed() && raw.result()==wirecheck::RawStageConnection::Result::Staged);
   close(listener);return 0;}assert(status==202);service.stepSyncStageHash();
 }
 close(listener);return 2;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(fixture['fixture']['storage'])
    (directory / 'mbedtls').mkdir()
    (directory / 'mbedtls/sha256.h').write_text(fixture['sha_shim'])
    (directory / 'server.cpp').write_text(code)
    executable = directory / 'server'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'server.cpp'), *crypto_flags,
                    '-o', str(executable)], check=True, timeout=30)
    server = subprocess.Popen([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if not select.select([server.stdout], [], [], 5)[0]:
            raise TimeoutError('Local stage and verification server did not start')
        raw_port, hash_port = map(int, server.stdout.readline().split())
        data = bytes((i * 37 + i // 251) % 251 for i in range(9000))
        expected = hashlib.sha256(data).hexdigest()
        name = '__xsync_0123456789abcdef0123456789abcdef.part'
        upload_stage('http://127.0.0.1', raw_port, name, 'a' * 64, io.BytesIO(data), len(data), expected)
        confirmations = []
        actual = verify_stage(f'http://127.0.0.1:{hash_port}', name, len(data), 'a' * 64,
                              lambda: confirmations.append(True), timeout=10)
        assert actual == expected and confirmations == [True, True]
        _, errors = server.communicate(timeout=5)
        assert server.returncode == 0, errors
    except BaseException:
        if server.poll() is None:
            server.terminate()
        _, errors = server.communicate(timeout=5)
        if errors:
            print('Local server error:', errors, file=sys.stderr)
        raise
    finally:
        if server.poll() is None:
            server.terminate()
            server.wait(timeout=5)

print('Mac staging and SHA-256 verification passed against local C++ handlers')
