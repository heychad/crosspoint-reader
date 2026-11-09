"""Exercise the inactive raw staging session and real ExclusiveStage methods."""
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
#include <cstring>
#include <fcntl.h>
#include <vector>
struct HalStorage;
struct HalFile {
 struct ReadCursor{uint64_t position=0;uint32_t cluster=0;};
 HalStorage* store=nullptr;size_t offset=0;
 explicit operator bool() const{return store;} bool close();
 bool isDirectory(){return false;} uint64_t fileSize64();
 bool seek64(uint64_t p); size_t write(const uint8_t*,size_t);
 bool captureReadCursor(ReadCursor& c){c.position=offset;c.cluster=0;return store;}
 bool restoreReadCursor(const ReadCursor& c){offset=c.position;return store;}
 int read(void*,size_t);
 bool sync();
};
struct HalStorage {
 enum class PathStatus{Missing,Found};struct MutationMarker{unsigned value=0;};struct MutationSnapshot{unsigned value=0;};
 std::vector<uint8_t> bytes;bool exists=false,writeOk=true,syncOk=true,readOk=true,snapshotAllowed=true;unsigned generation=0,lockDepth=0;
 unsigned opens=0,closes=0,seeks=0;
 void(*beforeLock)(void*)=nullptr;void* beforeLockContext=nullptr;
 bool withStorageLock(bool(*f)(void*),void* p){if(!lockDepth && beforeLock)beforeLock(beforeLockContext);
  ++lockDepth;bool result=f(p);--lockDepth;return result;}
 PathStatus lookupPath(const char*){return exists?PathStatus::Found:PathStatus::Missing;}
 bool monitoredStorageHealthy(){return true;}
 HalFile open(const char*,int flags){++opens;if(flags&O_CREAT){if(exists)return {};exists=true;++generation;}
  return exists?HalFile{this}:HalFile{};}
 bool captureMutationMarker(MutationMarker& m){m.value=generation;return true;}
 bool beginMutationSnapshot(MutationSnapshot& s){s.value=generation;return snapshotAllowed;}
 bool mutationMarkerUnchanged(const MutationMarker& m){return m.value==generation;}
 bool mutationSnapshotUnchanged(const MutationSnapshot& s){return s.value==generation;}
};
uint64_t HalFile::fileSize64(){return store->bytes.size();}
bool HalFile::close(){if(store){++store->closes;store=nullptr;}return true;}
bool HalFile::seek64(uint64_t p){++store->seeks;offset=p;return true;}
size_t HalFile::write(const uint8_t* p,size_t n){if(!store->writeOk)return 0;
 if(offset+n>store->bytes.size())store->bytes.resize(offset+n);
 for(size_t i=0;i<n;i++)store->bytes[offset+i]=p[i];offset+=n;++store->generation;return n;}
int HalFile::read(void* p,size_t n){if(!store->readOk || offset+n>store->bytes.size())return -1;
 std::memcpy(p,store->bytes.data()+offset,n);offset+=n;return static_cast<int>(n);}
bool HalFile::sync(){return store->syncOk;}
'''
CODE = r'''
#include <cassert>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include "RawStageSession.h"
#include "RawStagePump.h"
#include "RawStageSocket.h"
#include "RawStageConnection.h"
#include "RawStageListener.h"
using wirecheck::RawStageSession;
using wirecheck::RawStagePump;
using wirecheck::RawStageSocket;
using wirecheck::RawStageConnection;
using wirecheck::RawStageListener;
struct Context {bool active=true;int checks=0;int failAt=0;uint32_t now=0;std::string data;size_t cursor=0;bool eof=false;};
bool active(void* p){return static_cast<Context*>(p)->active;}
bool identity(void* p,const char* id){auto& c=*static_cast<Context*>(p);++c.checks;
 return std::string(id)==std::string(64,'a') && c.checks!=c.failAt;}
uint32_t clockNow(void* p){return static_cast<Context*>(p)->now;}
int readInput(void* p,uint8_t* out,size_t max){auto& c=*static_cast<Context*>(p);
 size_t n=std::min(max,c.data.size()-c.cursor);if(!n)return c.eof?-1:0;
 std::memcpy(out,c.data.data()+c.cursor,n);c.cursor+=n;return static_cast<int>(n);}
std::string request(size_t size){return "PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: "+
 std::to_string(size)+"\r\nContent-Type: application/octet-stream\r\nX-Xsync-Device: "+
 std::string(64,'a')+"\r\nX-Xsync-Stage: __xsync_0123456789abcdef0123456789abcdef.part\r\n\r\n";}
size_t send(RawStageSession& s,const std::string& x){return s.consume(reinterpret_cast<const uint8_t*>(x.data()),x.size());}
int main(){
 {HalStorage d;Context c;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  auto h=request(5);for(char ch:h)assert(send(s,std::string(1,ch))==1);
  assert(s.state()==RawStageSession::State::Body && d.exists && d.bytes.empty());
  assert(send(s,"helloEXTRA")==5 && s.state()==RawStageSession::State::Complete);
  assert(std::string(d.bytes.begin(),d.bytes.end())=="hello" && c.checks==2);}
 {HalStorage d;Context c;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  assert(send(s,request(0))>0 && s.state()==RawStageSession::State::Complete && d.exists);}
 {HalStorage d;Context c;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  send(s,request(5));send(s,"ab");s.cancel();assert(s.state()==RawStageSession::State::Failed);
  assert(d.exists && d.bytes.empty());}
 {HalStorage d;Context c;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  send(s,request(5));send(s,"abcd");c.active=false;assert(send(s,"e")==0);
  assert(s.state()==RawStageSession::State::Failed && d.bytes.size()==4);}
 {HalStorage d;Context c;c.failAt=1;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  send(s,request(5));assert(s.state()==RawStageSession::State::Failed && !d.exists);}
 {HalStorage d;Context c;c.failAt=2;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  send(s,request(1)+"a");assert(s.state()==RawStageSession::State::Failed && d.bytes.size()==1);}
 {HalStorage d;Context c;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  send(s,request(5));d.writeOk=false;send(s,"abcd");assert(s.state()==RawStageSession::State::Failed);}
 {HalStorage d;Context c;uint8_t b[4];RawStageSession s(d,b,sizeof(b),{&c,active,identity});
  send(s,request(5));++d.generation;send(s,"abcd");assert(s.state()==RawStageSession::State::Failed);}
 {HalStorage d;Context c;RawStageSession s(d,nullptr,0,{&c,active,identity});
  assert(s.state()==RawStageSession::State::Failed);}
 {HalStorage d;Context c;c.data=request(5)+"hello";uint8_t b[4];
  RawStagePump p(d,b,sizeof(b),{&c,clockNow,readInput,active,identity},c.now);
  while(p.poll()==RawStagePump::Result::Pending){} assert(p.state()==RawStagePump::Result::Complete);
  assert(std::string(d.bytes.begin(),d.bytes.end())=="hello");}
 {HalStorage d;Context c;uint8_t b[4];RawStagePump p(d,b,sizeof(b),{&c,clockNow,readInput,active,identity},c.now);
  c.now=14999;assert(p.poll()==RawStagePump::Result::Pending);c.now=15000;assert(p.poll()==RawStagePump::Result::Failed);}
 {HalStorage d;Context c;c.now=0xfffffff0;uint8_t b[4];
  RawStagePump p(d,b,sizeof(b),{&c,clockNow,readInput,active,identity},c.now);
  c.now+=15000;assert(p.poll()==RawStagePump::Result::Failed);}
 {HalStorage d;Context c;c.data=request(5);uint8_t b[4];
  RawStagePump p(d,b,sizeof(b),{&c,clockNow,readInput,active,identity},c.now);
  while(c.cursor<c.data.size())assert(p.poll()==RawStagePump::Result::Pending);
  assert(d.exists);c.now=30000;assert(p.poll()==RawStagePump::Result::Failed);}
 {HalStorage d;Context c;c.eof=true;uint8_t b[4];RawStagePump p(d,b,sizeof(b),{&c,clockNow,readInput,active,identity},c.now);
  assert(p.poll()==RawStagePump::Result::Failed && !d.exists);}
 {HalStorage d;Context c;uint8_t b[4];RawStagePump p(d,b,sizeof(b),{&c,clockNow,readInput,active,identity},c.now);
  c.active=false;assert(p.poll()==RawStagePump::Result::Failed && !d.exists);}
 {int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);HalStorage d;Context c;uint8_t b[4];
  RawStageSocket socket(d,fd[0],b,sizeof(b),{&c,clockNow,active,identity},c.now);
  assert(socket.poll()==RawStagePump::Result::Pending);auto h=request(5);
  assert(write(fd[1],h.data(),h.size())==static_cast<ssize_t>(h.size()));
  assert(write(fd[1],"hello",5)==5);while(socket.poll()==RawStagePump::Result::Pending){}
  assert(std::string(d.bytes.begin(),d.bytes.end())=="hello" && c.checks==2);
  close(fd[0]);close(fd[1]);}
 {int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);HalStorage d;Context c;uint8_t b[4];
  RawStageSocket socket(d,fd[0],b,sizeof(b),{&c,clockNow,active,identity},c.now);
  close(fd[1]);assert(socket.poll()==RawStagePump::Result::Failed);close(fd[0]);}
 {int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);HalStorage d;Context c;uint8_t b[4];
  RawStageConnection conn(d,fd[0],b,sizeof(b),{&c,clockNow,active,identity},c.now);
  auto data=request(5)+"hello";assert(write(fd[1],data.data(),data.size())==static_cast<ssize_t>(data.size()));
  assert(fcntl(fd[0],F_SETFL,fcntl(fd[0],F_GETFL,0)|O_NONBLOCK)==0);
  while(conn.poll()==RawStageConnection::Result::Pending){}assert(conn.state()==RawStageConnection::Result::Staged);
  char response[512]{};const auto n=read(fd[1],response,sizeof(response));assert(n>0);
  const std::string reply(response,n);assert(reply.find("HTTP/1.1 201 Created\r\n")==0);
  assert(reply.find("Content-Length: 15\r\n")!=std::string::npos && reply.ends_with("{\"staged\":true}"));
  assert(std::string(d.bytes.begin(),d.bytes.end())=="hello");close(fd[0]);close(fd[1]);}
 {int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);HalStorage d;Context c;uint8_t b[4];
  RawStageConnection conn(d,fd[0],b,sizeof(b),{&c,clockNow,active,identity},c.now);
  const std::string data="GET / HTTP/1.1\r\n";assert(write(fd[1],data.data(),data.size())==static_cast<ssize_t>(data.size()));
  while(conn.poll()==RawStageConnection::Result::Pending){}assert(conn.state()==RawStageConnection::Result::Rejected);
  char response[512]{};const auto n=read(fd[1],response,sizeof(response));assert(n>0);
  assert(std::string(response,n).find("HTTP/1.1 400 Bad Request\r\n")==0 && !d.exists);close(fd[0]);close(fd[1]);}
 {int fd[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,fd)==0);HalStorage d;Context c;uint8_t b[4];
  auto data=request(5)+"hello";assert(write(fd[1],data.data(),data.size())==static_cast<ssize_t>(data.size()));
  assert(fcntl(fd[0],F_SETFL,fcntl(fd[0],F_GETFL,0)|O_NONBLOCK)==0);
  char fill[4096]{};while(send(fd[0],fill,sizeof(fill),MSG_DONTWAIT)>0){}assert(errno==EAGAIN || errno==EWOULDBLOCK);
  RawStageConnection conn(d,fd[0],b,sizeof(b),{&c,clockNow,active,identity},c.now);
  while(c.checks<2)assert(conn.poll()==RawStageConnection::Result::Pending);
  c.now=2000;assert(conn.poll()==RawStageConnection::Result::ResponseLost);
  assert(std::string(d.bytes.begin(),d.bytes.end())=="hello");close(fd[0]);close(fd[1]);}
 {HalStorage d;Context c;RawStageListener server(d,{&c,clockNow,active,identity});assert(server.begin(0));
  assert(server.localPort()!=0 && server.running());int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  address.sin_port=htons(server.localPort());assert(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
  const auto data=request(5)+"hello";assert(write(fd,data.data(),data.size())==static_cast<ssize_t>(data.size()));
  for(int i=0;i<1000 && !server.completed();++i)server.poll();
  assert(server.completed() && server.result()==RawStageConnection::Result::Staged && !server.busy());
  char response[512]{};const auto n=read(fd,response,sizeof(response));assert(n>0);
  assert(std::string(response,n).starts_with("HTTP/1.1 201 Created\r\n"));
  assert(std::string(d.bytes.begin(),d.bytes.end())=="hello");close(fd);server.stop();assert(!server.running());}
 {HalStorage d;Context c;RawStageListener server(d,{&c,clockNow,active,identity});assert(server.begin(0));
  int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);sockaddr_in address{};address.sin_family=AF_INET;
  address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(server.localPort());
  assert(connect(fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
  const auto header=request(5);assert(write(fd,header.data(),header.size())==static_cast<ssize_t>(header.size()));
  for(int i=0;i<1000 && !d.exists;++i)server.poll();assert(d.exists && server.busy());
  server.stop();assert(!server.running() && !server.busy() && d.bytes.empty());close(fd);}
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
    subprocess.run([str(executable)], check=True, timeout=10)
print('Raw staging session lifecycle passed')
