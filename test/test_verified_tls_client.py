"""Exercise production TLS setup with controlled wolfSSL/transport adapters."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patch_tls',root/'scripts/patch_freeink_tls.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)
patch.apply_patch(root)
source=(root/'freeink-sdk'/patch.TARGET).read_text()

def extract(signature):
    start=source.index(signature);opening=source.index('{',start);depth=0
    for index in range(opening,len(source)):
        depth+=(source[index]=='{')-(source[index]=='}')
        if not depth:return source[start:index+1]
    raise RuntimeError(signature)

methods='\n'.join(extract(s) for s in ('void SecureClient::setCACert(',
                                     'void SecureClient::setInsecure(',
                                     'int SecureClient::connectWithMethod(',
                                     'int SecureClient::connect(const char*'))
code=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
constexpr int WOLFSSL_SUCCESS=1,WOLFSSL_VERIFY_NONE=0,WOLFSSL_VERIFY_PEER=1,WOLFSSL_FILETYPE_PEM=1,WOLFSSL_SNI_HOST_NAME=0;
struct WOLFSSL_METHOD{};struct WOLFSSL_CTX{int mode=WOLFSSL_VERIFY_PEER;bool ca=false;};
struct WOLFSSL{WOLFSSL_CTX* ctx;std::string host;};
struct Controls {bool caOk=true,domainSetupOk=true,chainValid=true,dateValid=true,hostValid=true;
 bool ctxOk=true,sslOk=true,firstHandshakeFails=false,tcpOk=true;
 int contexts=0,sessions=0,caLoads=0,domainChecks=0,handshakes=0,tcpCalls=0;std::vector<std::string> domains;
} controls;
struct SerialStub {explicit operator bool()const{return false;}template<class...T>void printf(T...){}void println(const char*){}} Serial;
struct Heap{unsigned getFreeHeap()const{return 100000;}} ESP;
uint32_t clockMs=1;uint32_t millis(){return clockMs;}void delay(unsigned n){clockMs+=n;}
struct Transport {bool open=false;void setConnectionTimeout(uint32_t){}bool connect(const char*,uint16_t){++controls.tcpCalls;return open=controls.tcpOk;}
 void stop(){open=false;}bool connected()const{return open;}};
WOLFSSL_CTX* wolfSSL_CTX_new(WOLFSSL_METHOD*){if(!controls.ctxOk)return nullptr;++controls.contexts;return new WOLFSSL_CTX;}
void wolfSSL_CTX_free(WOLFSSL_CTX* p){--controls.contexts;delete p;}
void wolfSSL_CTX_set_verify(WOLFSSL_CTX* ctx,int mode,void*){ctx->mode=mode;}
int wolfSSL_CTX_load_verify_buffer(WOLFSSL_CTX* ctx,const unsigned char*,size_t,int){++controls.caLoads;ctx->ca=controls.caOk;return controls.caOk?1:0;}
int wcSend(WOLFSSL*,char*,int,void*){return 0;}int wcRecv(WOLFSSL*,char*,int,void*){return 0;}
void wolfSSL_SetIORecv(WOLFSSL_CTX*,int(*)(WOLFSSL*,char*,int,void*)){}
void wolfSSL_SetIOSend(WOLFSSL_CTX*,int(*)(WOLFSSL*,char*,int,void*)){}
WOLFSSL* wolfSSL_new(WOLFSSL_CTX* ctx){if(!controls.sslOk)return nullptr;++controls.sessions;return new WOLFSSL{ctx,{}};}
void wolfSSL_free(WOLFSSL* ssl){--controls.sessions;delete ssl;}
void wolfSSL_SetIOReadCtx(WOLFSSL*,Transport*){}void wolfSSL_SetIOWriteCtx(WOLFSSL*,Transport*){}
int wolfSSL_check_domain_name(WOLFSSL* ssl,const char* host){++controls.domainChecks;controls.domains.push_back(host);
 if(!controls.domainSetupOk)return 0;ssl->host=host;return 1;}
void wolfSSL_UseSNI(WOLFSSL*,int,const char*,size_t){}
int wolfSSL_connect(WOLFSSL* ssl){++controls.handshakes;
 if(controls.firstHandshakeFails && controls.handshakes==1)return -1;
 if(ssl->ctx->mode==WOLFSSL_VERIFY_PEER && (!ssl->ctx->ca || !controls.chainValid || !controls.dateValid || (!ssl->host.empty()&&!controls.hostValid)))return -1;
 return 1;}
int wolfSSL_get_error(WOLFSSL*,int){return -10;}bool isWantIo(int){return false;}
const char* wolfSSL_get_version(WOLFSSL*){return "TLS";}const char* wolfSSL_get_cipher(WOLFSSL*){return "test";}
void* wolfSSLv23_client_method(){static WOLFSSL_METHOD method;return &method;}
void* wolfTLSv1_2_client_method(){return wolfSSLv23_client_method();}
bool hostResolves(const char*){return true;}
struct SecureClient {
 const char* _rootCA=nullptr;bool _insecure=false,_tcpConnected=false,_aborted=false,_connected=false;
 void* _ctx=nullptr;void* _ssl=nullptr;Transport _transport;
 ~SecureClient(){stop();}uint32_t getTimeout()const{return 100;}bool abortRequested(){return false;}
 void stop(){if(_ssl){wolfSSL_free(static_cast<WOLFSSL*>(_ssl));_ssl=nullptr;}
  if(_ctx){wolfSSL_CTX_free(static_cast<WOLFSSL_CTX*>(_ctx));_ctx=nullptr;}_transport.stop();_connected=false;}
 void setCACert(const char*);void setInsecure();int connectWithMethod(const char*,uint16_t,void*,const char*);int connect(const char*,uint16_t);
};
''' + methods + r'''
int main(){
 // CA selection reverses a prior explicit insecure mode and binds the requested hostname.
 {SecureClient client;client.setInsecure();client.setCACert("root");assert(!client._insecure);
  assert(client.connect("api.github.com",443)==1);assert(controls.caLoads==1 && controls.domainChecks==1);
  assert(controls.domains.back()=="api.github.com");}assert(controls.contexts==0 && controls.sessions==0);
 // Chain, certificate dates, and hostname failures must remain failures on the TLS 1.2 retry.
 for(int failure=0;failure<3;++failure){controls={};SecureClient client;client.setCACert("root");
  if(failure==0)controls.chainValid=false;else if(failure==1)controls.dateValid=false;else controls.hostValid=false;
  assert(client.connect("api.github.com",443)==0);assert(controls.handshakes==2 && controls.caLoads==2 && controls.domainChecks==2);
  assert(controls.contexts==0 && controls.sessions==0 && !client._connected && !client._transport.connected());
 }
 // Failed CA loading or hostname allocation aborts before a handshake, including retry.
 for(bool domain:{false,true}){controls={};SecureClient client;client.setCACert("root");
  if(domain)controls.domainSetupOk=false;else controls.caOk=false;
  assert(client.connect("api.github.com",443)==0);assert(controls.handshakes==0 && controls.contexts==0 && controls.sessions==0);
  assert(controls.caLoads==2 && controls.domainChecks==(domain?2:0));
 }
 // A successful fallback still verifies the same CA and hostname.
 {controls={};controls.firstHandshakeFails=true;SecureClient client;client.setCACert("root");
  assert(client.connect("release-assets.githubusercontent.com",443)==1);
  assert(controls.handshakes==2 && controls.domainChecks==2 && controls.caLoads==2);
  assert(controls.domains[0]==controls.domains[1]);}assert(controls.contexts==0 && controls.sessions==0);
 // Existing explicit insecure callers keep their behavior and do not load/check a CA.
 {controls={};controls.chainValid=false;controls.dateValid=false;controls.hostValid=false;SecureClient client;
  client.setCACert("root");client.setInsecure();assert(client.connect("catalog.local",443)==1);
  assert(controls.caLoads==0 && controls.domainChecks==0);}assert(controls.contexts==0 && controls.sessions==0);
 for(bool context:{false,true}){controls={};SecureClient client;client.setCACert("root");
  if(context)controls.ctxOk=false;else controls.sslOk=false;
  assert(client.connect("api.github.com",443)==0);assert(controls.contexts==0 && controls.sessions==0 && controls.handshakes==0);}
 {controls={};controls.tcpOk=false;SecureClient client;client.setCACert("root");
  assert(client.connect("api.github.com",443)==0);assert(controls.tcpCalls==1 && controls.caLoads==0 && controls.contexts==0);}
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',*shlex.split(os.environ.get('CXXFLAGS','')),
                    str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Verified TLS setup, retry, failure cleanup, and insecure compatibility checks passed')
