"""Exercise the bounded metadata validator for the inactive raw stage transport."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STUB = r'''
#pragma once
#include <cstddef>
#include <cstdint>
constexpr int O_CREAT=1, O_EXCL=2, O_WRONLY=4, O_RDONLY=8;
struct HalFile {
 explicit operator bool() const {return true;} bool close(){return true;}
 bool isDirectory(){return false;} uint64_t fileSize64(){return 0;}
 bool seek64(uint64_t){return true;} size_t write(const uint8_t*,size_t n){return n;}
 bool sync(){return true;}
};
struct HalStorage {
 enum class PathStatus {Missing}; struct MutationMarker {};
 bool withStorageLock(bool(*f)(void*),void* p){return f(p);}
 PathStatus lookupPath(const char*){return PathStatus::Missing;}
 bool monitoredStorageHealthy(){return true;} HalFile open(const char*,int){return {};}
 bool captureMutationMarker(MutationMarker&){return true;}
 bool mutationMarkerUnchanged(const MutationMarker&){return true;}
};
'''
CODE = r'''
#include <cassert>
#include <string>
#include "RawStageHeaders.h"
#include "RawStageRequest.h"
using wirecheck::RawStageHeaders;
using wirecheck::RawStageRequest;
const std::string id(64,'a');
const std::string name="__xsync_0123456789abcdef0123456789abcdef.part";
bool normal(RawStageHeaders& h,const std::string& length="42") {
 return h.add("Content-Length: "+length) && h.add("Content-Type: application/octet-stream") &&
        h.add("X-Xsync-Device: "+id) && h.add("X-Xsync-Stage: "+name);
}
std::string request(){
 return "PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: 3\r\n"
        "Content-Type: application/octet-stream\r\nX-Xsync-Device: "+id+
        "\r\nX-Xsync-Stage: "+name+"\r\n\r\n";
}
RawStageRequest::Result feed(RawStageRequest& parser,const std::string& data){
 for(unsigned char c:data)if(parser.consume(c)!=RawStageRequest::Result::Pending)return parser.state();
 return parser.state();
}
int main(){
 {RawStageHeaders h;assert(normal(h) && h.complete() && h.size()==42);
  assert(std::string(h.deviceId())==id && std::string(h.stageName())==name);}
 for(const std::string s:{"0","2147482623"}){RawStageHeaders h;assert(normal(h,s) && h.complete());}
 for(const std::string s:{"","00","-1","+1","42x","2147482624","9999999999999999999999999"}){
  RawStageHeaders h;assert(!normal(h,s) && !h.complete());}
 {RawStageHeaders h;assert(normal(h));assert(!h.add("content-length: 42") && !h.complete());}
 {RawStageHeaders h;assert(normal(h));assert(!h.add("Transfer-Encoding: chunked") && !h.complete());}
 {RawStageHeaders h;assert(!h.add("Content-Type: multipart/form-data") && !h.complete());}
 {RawStageHeaders h;assert(!h.add("X-Xsync-Device: "+std::string(64,'A')));}
 {RawStageHeaders h;assert(!h.add("X-Xsync-Stage: __xsync_0123456789abcdef0123456789abcdeg.part"));}
 {RawStageHeaders h;assert(!h.add(" folded: value"));}
 {RawStageHeaders h;assert(!h.add(std::string(513,'x')));}
 {RawStageHeaders h;for(int i=0;i<32;i++)assert(h.add("Other: ok"));assert(!h.add("Other: ok"));}
 {RawStageHeaders h;assert(!h.add(std::string("Other: x\0y",10)));}
 {RawStageHeaders h;assert(h.add("content-length: 5"));assert(!h.complete());}
 {RawStageRequest p;auto s=request();assert(feed(p,s.substr(0,s.size()-1))==RawStageRequest::Result::Pending);
  assert(feed(p,s.substr(s.size()-1))==RawStageRequest::Result::Complete);
  assert(p.metadata().size()==3 && std::string(p.metadata().stageName())==name);}
 {RawStageRequest p;assert(feed(p,"POST /api/sync/stage-raw HTTP/1.1\r\n")==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;assert(feed(p,"PUT /api/sync/stage-raw HTTP/1.1\n")==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;assert(feed(p,"PUT /api/sync/stage-raw HTTP/1.1\rX")==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;assert(feed(p,"PUT /api/sync/stage-raw HTTP/1.1\r\n\r\n")==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;auto s=request();s.replace(s.find("application/octet-stream"),24,"multipart/form-data");
  assert(feed(p,s)==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;assert(feed(p,std::string(513,'x'))==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;auto s=request();s.insert(s.find("\r\n\r\n"),"\r\nTransfer-Encoding: chunked");
  assert(feed(p,s)==RawStageRequest::Result::Rejected);}
 {RawStageRequest p;assert(feed(p,request())==RawStageRequest::Result::Complete);
  assert(p.consume('X')==RawStageRequest::Result::Complete && p.metadata().size()==3);}
}
'''

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(STUB)
    (directory / 'test.cpp').write_text(CODE)
    executable = directory / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'test.cpp'), '-o', str(executable)],
                   check=True)
    subprocess.run([str(executable)], check=True)
print('Raw staging header validation passed')
