"""Test the inactive stage snapshot reader with a deterministic digest stub."""
import ast
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
tree = ast.parse((ROOT / 'test/stage_http/test_raw_session.py').read_text())
storage = next(ast.literal_eval(node.value) for node in tree.body
               if isinstance(node, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'STUB' for t in node.targets))
sha = r'''
#pragma once
#include <cstddef>
#include <cstdint>
struct mbedtls_sha256_context{uint8_t sum=0;};
inline void mbedtls_sha256_init(mbedtls_sha256_context* c){c->sum=0;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
inline int mbedtls_sha256_starts(mbedtls_sha256_context* c,int){c->sum=0;return 0;}
inline int mbedtls_sha256_update(mbedtls_sha256_context* c,const uint8_t* p,size_t n){
 for(size_t i=0;i<n;i++)c->sum+=p[i];return 0;}
inline int mbedtls_sha256_finish(mbedtls_sha256_context* c,uint8_t* out){
 for(int i=0;i<32;i++)out[i]=c->sum;return 0;}
'''
code = r'''
#include <cassert>
#include <cstring>
#include <string>
#include "StageHash.h"
#include "StageHashJob.h"
#include "CooperativeStageHash.h"
using wirecheck::StageHash;
using wirecheck::StageHashJob;
constexpr const char* name="__xsync_0123456789abcdef0123456789abcdef.part";
struct Context{bool active=true;bool identity=true;};
bool active(void* p){return static_cast<Context*>(p)->active;}
bool verify(void* p,const char* id){return static_cast<Context*>(p)->identity && std::string(id)==std::string(64,'a');}
int main(){
 {HalStorage d;d.exists=true;d.bytes={'h','e','l','l','o'};uint8_t buffer[3],out[32]{};
  StageHash hash(d,buffer,sizeof(buffer));assert(hash.begin(name,5));assert(!hash.copyDigest(out));
  assert(hash.step()==StageHash::State::Reading);assert(hash.step()==StageHash::State::Complete);
  assert(d.seeks==0 && d.opens==3 && d.closes==3);
  assert(hash.copyDigest(out));for(auto byte:out)assert(byte==static_cast<uint8_t>('h'+'e'+'l'+'l'+'o'));}
 {HalStorage d;d.exists=true;uint8_t b[3],out[32];StageHash h(d,b,sizeof(b));
  assert(h.begin(name,0) && h.step()==StageHash::State::Complete && h.copyDigest(out));
  for(auto byte:out)assert(byte==0);}
 {HalStorage d;d.exists=true;d.bytes={'a','b','c','d'};uint8_t b[2],out[32];StageHash h(d,b,sizeof(b));
  assert(h.begin(name,4) && h.step()==StageHash::State::Reading);++d.generation;
  assert(h.step()==StageHash::State::Failed && !h.copyDigest(out));}
 {HalStorage d;d.exists=true;d.bytes={'a'};uint8_t b[2];StageHash h(d,b,sizeof(b));
  assert(h.begin(name,1));d.readOk=false;assert(h.step()==StageHash::State::Failed);}
 {HalStorage d;d.exists=true;d.bytes={'a'};uint8_t b[2];StageHash h(d,b,sizeof(b));assert(!h.begin(name,2));}
 {HalStorage d;uint8_t b[2];StageHash h(d,b,sizeof(b));assert(!h.begin(name,0));}
 {HalStorage d;d.exists=true;d.snapshotAllowed=false;uint8_t b[2];StageHash h(d,b,sizeof(b));
  assert(!h.begin(name,0) && d.opens==0);}
 {HalStorage d;d.exists=true;uint8_t b[2];StageHash h(d,b,sizeof(b));assert(!h.begin("../book",0));}
 {HalStorage d;d.exists=true;uint8_t b[2],out[32];StageHash h(d,b,sizeof(b));
  assert(h.begin(name,0));++d.generation;assert(h.step()==StageHash::State::Failed && !h.copyDigest(out));}
 {HalStorage d;d.exists=true;uint8_t b[2],out[32];StageHash h(d,b,sizeof(b));
  assert(h.begin(name,0));h.cancel();assert(h.state()==StageHash::State::Failed && !h.copyDigest(out));}
 {HalStorage d;d.exists=true;uint8_t b[2],out[32];StageHash h(d,b,sizeof(b));
  assert(h.begin(name,0) && h.step()==StageHash::State::Complete);++d.generation;
  assert(!h.copyDigest(out) && h.state()==StageHash::State::Failed);}
 {HalStorage d;d.exists=true;d.bytes={'a','b','c'};Context c;uint8_t b[2];char out[65];
  StageHashJob job(d,b,sizeof(b),{&c,active,verify});assert(job.begin(name,3,std::string(64,'a').c_str()));
  assert(job.step()==StageHashJob::State::Reading && job.step()==StageHashJob::State::Ready);
  std::string expected;for(int i=0;i<32;++i)expected+="26";
  assert(job.copyHex(out) && std::string(out)==expected);}
 {HalStorage d;d.exists=true;Context c;uint8_t b[2];char out[65];
  StageHashJob job(d,b,sizeof(b),{&c,active,verify});assert(job.begin(name,0,std::string(64,'a').c_str()));
  assert(job.step()==StageHashJob::State::Ready);c.identity=false;assert(!job.copyHex(out));}
 {HalStorage d;d.exists=true;Context c;uint8_t b[2];char out[65];
  StageHashJob job(d,b,sizeof(b),{&c,active,verify});assert(job.begin(name,0,std::string(64,'a').c_str()));
  assert(job.step()==StageHashJob::State::Ready);++d.generation;assert(!job.copyHex(out));}
 {HalStorage d;d.exists=true;Context c;uint8_t b[2];StageHashJob job(d,b,sizeof(b),{&c,active,verify});
  assert(!job.begin(name,0,std::string(64,'A').c_str()));}
 {HalStorage d;d.exists=true;Context c;uint8_t b[2];StageHashJob job(d,b,sizeof(b),{&c,active,verify});
  assert(job.begin(name,0,std::string(64,'a').c_str()));c.active=false;
  assert(job.step()==StageHashJob::State::Failed);}
 {HalStorage d;d.exists=true;d.bytes={'a','b','c','d','e'};Context c;uint8_t b[2];char out[65];
  using Job=wirecheck::CooperativeStageHash;const auto id=std::string(64,'a');Job j(d,b,sizeof(b),{&c,active,verify});
  assert(j.begin(name,5,id.c_str(),100));assert(!j.matches(name,4,id.c_str()));
  assert(j.step(101)==Job::State::Reading && j.step(102)==Job::State::Reading);
  assert(j.step(103)==Job::State::Ready);assert(!j.copyHex(name,4,id.c_str(),104,out));
  assert(j.copyHex(name,5,id.c_str(),104,out));++d.generation;
  assert(!j.copyHex(name,5,id.c_str(),105,out) && j.state()==Job::State::Failed);}
 {HalStorage d;d.exists=true;Context c;uint8_t b[2];using Job=wirecheck::CooperativeStageHash;
  const auto id=std::string(64,'a');Job j(d,b,sizeof(b),{&c,active,verify});
  assert(j.begin(name,0,id.c_str(),0xfffffff0U));assert(j.step(0x10U)==Job::State::Ready);
  assert(j.step(0xfffffff0U+3600000U)==Job::State::Failed);}
 {HalStorage d;d.exists=true;Context c;uint8_t b[2];using Job=wirecheck::CooperativeStageHash;
  const auto id=std::string(64,'a');Job j(d,b,sizeof(b),{&c,active,verify});
  assert(j.begin(name,0,id.c_str(),0));j.cancel();assert(j.step(1)==Job::State::Failed);}
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
print('Stage snapshot reading, invalidation and failure checks passed with a digest stub')
