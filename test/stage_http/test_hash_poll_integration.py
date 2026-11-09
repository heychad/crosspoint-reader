"""Connect the Mac polling client to the extracted C++ handler over localhost."""
import os
import hashlib
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

fixture = runpy.run_path(str(ROOT / 'test/stage_http/test_stage_hash_http.py'))
real_sha = os.environ.get('CROSSPOINT_TEST_REAL_SHA') == '1'
crypto_flags = []
if real_sha:
    try:
        crypto_flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libcrypto'],
                                                          text=True, stderr=subprocess.DEVNULL))
    except (OSError, subprocess.CalledProcessError):
        print('Host OpenSSL is unavailable; real SHA-256 integration skipped')
        raise SystemExit(77)
sha_shim = r'''
#pragma once
#include <openssl/evp.h>
#include <cstddef>
#include <cstdint>
struct mbedtls_sha256_context {EVP_MD_CTX* ctx;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->ctx=EVP_MD_CTX_new();}
inline void mbedtls_sha256_free(mbedtls_sha256_context* c){EVP_MD_CTX_free(c->ctx);}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){return c->ctx && EVP_DigestInit_ex(c->ctx,EVP_sha256(),nullptr)==1?0:-1;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const uint8_t* p,size_t n){return EVP_DigestUpdate(c->ctx,p,n)==1?0:-1;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,uint8_t* out){unsigned n=0;return EVP_DigestFinal_ex(c->ctx,out,&n)==1 && n==32?0:-1;}
'''
prefix = fixture['code'].split('int main(){', 1)[0]
code = prefix + r'''
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
int main(){
 int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);sockaddr_in address{};
 address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=0;
 assert(bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0 && listen(listener,1)==0);
 socklen_t length=sizeof(address);assert(getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
 std::printf("%u\n",ntohs(address.sin_port));std::fflush(stdout);
 CrossPointWebServer service;const std::string name="__xsync_0123456789abcdef0123456789abcdef.part";
 service.server->args={{"name",name},{"size","9000"},{"deviceId",std::string(64,'a')}};
 Storage.exists=true;Storage.bytes.resize(9000);
 for(size_t i=0;i<Storage.bytes.size();++i)Storage.bytes[i]=static_cast<uint8_t>((i*37+i/251)%251);
 for(int requests=0;requests<8;++requests){
  const int client=accept(listener,nullptr,nullptr);assert(client>=0);char request[1024]{};std::string line;
  while(line.find("\r\n\r\n")==std::string::npos){const int count=static_cast<int>(recv(client,request,sizeof(request),0));
   assert(count>0);line.append(request,count);assert(line.size()<4096);}
  const std::string expected="GET /api/sync/stage-hash?name="+name+"&size=9000&deviceId="+std::string(64,'a')+" HTTP/1.1";
  assert(line.starts_with(expected));service.handleSyncStageHash();const int status=service.server->status;
  const std::string body=service.server->body;
  const std::string response="HTTP/1.1 "+std::to_string(status)+" OK\r\nContent-Length: "+
      std::to_string(body.size())+"\r\nConnection: close\r\n\r\n"+body;
  size_t sent=0;while(sent<response.size()){const auto n=send(client,response.data()+sent,response.size()-sent,0);
   assert(n>0);sent+=static_cast<size_t>(n);}close(client);
  if(status==200){assert(requests==3);close(listener);return 0;}assert(status==202);service.stepSyncStageHash();
 }
 close(listener);return 2;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(fixture['storage'])
    (directory / 'mbedtls').mkdir()
    (directory / 'mbedtls/sha256.h').write_text(sha_shim if real_sha else fixture['sha'])
    (directory / 'server.cpp').write_text(code)
    executable = directory / 'server'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'server.cpp'), *crypto_flags,
                    '-o', str(executable)],
                   check=True, timeout=30)
    server = subprocess.Popen([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if not select.select([server.stdout], [], [], 5)[0]:
            raise TimeoutError('Local verification handler did not start')
        port = int(server.stdout.readline().strip())
        confirmations = []
        digest = verify_stage(f'http://127.0.0.1:{port}', '__xsync_0123456789abcdef0123456789abcdef.part',
                              9000, 'a' * 64, lambda: confirmations.append(True), timeout=10)
        data = bytes((i * 37 + i // 251) % 251 for i in range(9000))
        expected = hashlib.sha256(data).hexdigest() if real_sha else f'{sum(data) % 256:02x}' * 32
        assert digest == expected and confirmations == [True, True]
        _, errors = server.communicate(timeout=5)
        assert server.returncode == 0, errors
    finally:
        if server.poll() is None:
            server.terminate()
            server.wait(timeout=5)

print('Mac polling and actual C++ handler exchange passed with simulated storage and '
      + ('host OpenSSL SHA-256' if real_sha else 'a digest stub'))
