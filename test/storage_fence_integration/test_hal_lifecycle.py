"""Exercise production HAL fence hooks with a lock and controlled file handles."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/hal/HalStorage.cpp').read_text()

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == '{': depth += 1
        elif source[i] == '}':
            depth -= 1
            if not depth: return source[start:i + 1]
    raise AssertionError(signature)

impl = source[source.index('class HalFile::Impl {'):source.index('\nHalFile::HalFile() = default;')]
constructors = source[source.index('HalFile::HalFile() = default;'):source.index('\nHalStorage::SnapshotRead HalStorage::readDirectoryRecord(')]
macro = source[source.index('#define HAL_STORAGE_WRAPPED_CALL'):source.index('\n\nstd::vector<String>', source.index('#define HAL_STORAGE_MUTATION_CALL'))]
signatures = [
    'bool HalStorage::beginMutationSnapshot(', 'bool HalStorage::mutationSnapshotUnchanged(',
    'HalStorage::SnapshotRead HalStorage::readDirectoryRecord(',
    'void HalStorage::prepareForDeepSleep(', 'bool HalStorage::beginUsbDrive(',
    'HalFile HalStorage::open(', 'HalFile HalStorage::openChecked(', 'bool HalStorage::writeFile(', 'bool HalStorage::ensureDirectoryExists(',
    'bool HalStorage::mkdir(', 'bool HalStorage::remove(',
    'bool HalStorage::rename(', 'bool HalStorage::replaceFile(', 'bool HalStorage::rmdir(',
    'bool HalStorage::removeDir(', 'bool HalStorage::openFileForRead(const char* moduleName, const char* path,',
    'bool HalStorage::openFileForWrite(const char* moduleName, const char* path,',
    'void HalFile::flush()', 'bool HalFile::sync()', 'bool HalFile::truncate(', 'size_t HalFile::write(const uint8_t*',
    'size_t HalFile::write(const void*', 'size_t HalFile::write(uint8_t',
    'bool HalFile::rename(', 'bool HalFile::close()',
]
functions = '\n'.join(function(sig) for sig in signatures)
code = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "StorageMutationFence.h"
bool failAllocation=false;
template<typename T,typename... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... args){
 if(failAllocation)return {};return std::make_unique<T>(std::forward<A>(args)...);
}
#define FREEINK_CAP_USB_MSC 1
#define FREEINK_DEVICE_X4PRO 1
#define LOG_ERR(...) ((void)0)
bool monitorFault=false;
extern "C" bool crosspoint_sdmmc_monitor_healthy(){return !monitorFault;}
constexpr int O_RDONLY=0, O_WRONLY=1, O_RDWR=2, O_ACCMODE=3, O_CREAT=4, O_TRUNC=8;
using oflag_t=int;
using String=std::string;
struct FileState {bool open=false,writable=false,closeOk=true,syncOk=true,writeError=false,shortWrite=false,truncateOk=true,renameOk=true,directory=false,readError=false;int readCount=0,readResult=32;};
struct FsFile {
 std::shared_ptr<FileState> state;
 explicit operator bool()const{return isOpen();}
 bool isOpen()const{return state && state->open;}
 bool isWritable()const{return isOpen() && state->writable;}
 bool isDirectory()const{return isOpen() && state->directory;}
 int getError()const{return state && state->readError?1:0;}
 int read(void*,size_t){++state->readCount;return state->readResult;}
 bool close(){if(!state || !state->open)return true;state->open=false;return state->closeOk;}
 void flush(){sync();}
 bool sync(){return !state || state->syncOk;}
 bool getWriteError()const{return state && state->writeError;}
 size_t write(const uint8_t*,size_t n){return state && state->shortWrite && n ? n-1 : n;}
 size_t write(const void*,size_t n){return state && state->shortWrite && n ? n-1 : n;}
 size_t write(uint8_t){return state && state->shortWrite ? 0 : 1;}
 bool truncate(uint64_t){return state && state->truncateOk;}
 bool rename(const char*){return state && state->renameOk;}
};
struct Card {
 bool mounted=true,failOpen=false,nextCloseOk=true,canDetach=true,failSdkWrite=false,pathExists=true,failMutation=false;
 uint64_t sdTotalBytes(){return 4096;} bool sdUsedBytes(uint64_t& bytes){bytes=1024;return true;}
 bool ready()const{return mounted;}
 bool exists(const char*)const{return pathExists;}
 FsFile open(const char*,int flags){if(failOpen){failOpen=false;return{};}return {std::make_shared<FileState>(FileState{true,(flags&O_ACCMODE)!=O_RDONLY,nextCloseOk})};}
 bool openFileForRead(const char*,const char* path,FsFile& f){f=open(path,O_RDONLY);return f.isOpen();}
 bool openFileForWrite(const char*,const char* path,FsFile& f){f=open(path,O_WRONLY|O_CREAT);return f.isOpen();}
 bool writeFile(const char*,const String&){return !failSdkWrite;} bool ensureDirectoryExists(const char*){return true;}
 bool mkdir(const char*,bool){return !failMutation;} bool remove(const char*){return !failMutation;}
 bool rename(const char*,const char*){return true;} bool replaceFile(const char*,const char*){return true;}
 bool rmdir(const char*){return true;} bool removeDir(const char*){return true;}
 void shutdown(){mounted=false;} int* detachFilesystemForRawAccess(){static int d;if(!canDetach)return nullptr;mounted=false;return &d;}
 bool begin(){mounted=true;return true;}
} SDCard;
struct Usb {bool begin(int*){return true;}void end(){}} usbMassStorage;
class HalStorage;
class HalFile {
 public:
 class Impl; std::unique_ptr<Impl> impl;
 HalFile(); explicit HalFile(std::unique_ptr<Impl>); ~HalFile();
 HalFile(HalFile&&); HalFile& operator=(HalFile&&);
 void flush();bool sync();bool truncate(uint64_t);size_t write(const uint8_t*,size_t);size_t write(const void*,size_t);
 size_t write(uint8_t);bool rename(const char*);bool close();
 explicit operator bool()const;
};
class HalStorage {
 friend class HalFile;
 checkedscan::StorageMutationFence mutationFence;
 public:
 using MutationSnapshot=checkedscan::StorageMutationFence::Snapshot;
 enum class SnapshotRead {Record, End, Changed, Failed};
 enum class PathStatus {Missing, Present, Failed};
 PathStatus checkedStatus=PathStatus::Failed;
 PathStatus lookupPath(const char*) {return checkedStatus;}
 class StorageLock {static std::recursive_mutex mutex;std::lock_guard<std::recursive_mutex> guard;
 public:StorageLock():guard(mutex){}};
 static HalStorage& getInstance(){static HalStorage value;return value;}
 uint64_t sdTotalBytes();bool sdUsedBytes(uint64_t&);
 bool observedSdmmcHealthy() const {return SDCard.ready() && !monitorFault;}
 bool beginMutationSnapshot(MutationSnapshot&);bool mutationSnapshotUnchanged(const MutationSnapshot&);
 SnapshotRead readDirectoryRecord(HalFile&,const MutationSnapshot&,uint8_t*);
 void prepareForDeepSleep();bool beginUsbDrive();
 HalFile open(const char*,oflag_t);HalFile openChecked(const char*,oflag_t);bool writeFile(const char*,const String&);bool ensureDirectoryExists(const char*);
 bool mkdir(const char*,bool);bool remove(const char*);
 bool rename(const char*,const char*);bool replaceFile(const char*,const char*);bool rmdir(const char*);
 bool removeDir(const char*);bool openFileForRead(const char*,const char*,HalFile&);
 bool openFileForWrite(const char*,const char*,HalFile&);
};
std::recursive_mutex HalStorage::StorageLock::mutex;
''' + macro + '\n' + impl + '\n' + constructors + '\n' + functions + r'''
HalFile::operator bool()const{return impl && impl->file.isOpen();}
int main(int argc,char** argv){
 assert(argc==2);std::string which=argv[1];auto& s=HalStorage::getInstance();
 HalStorage::MutationSnapshot before;
 assert(s.beginMutationSnapshot(before));
 if(which=="normal"){
  {auto reader=s.open("read",O_RDONLY);assert(s.mutationSnapshotUnchanged(before));assert(reader.close());}
  assert(s.mutationSnapshotUnchanged(before));
  auto writer=s.open("write",O_WRONLY|O_CREAT);assert(!s.mutationSnapshotUnchanged(before));
  HalStorage::MutationSnapshot during;assert(!s.beginMutationSnapshot(during));
  const uint8_t data[]={1,2};assert(writer.write(data,2)==2);writer.flush();assert(writer.truncate(1));
  auto moved=std::move(writer);assert(!s.beginMutationSnapshot(during));assert(moved.close());
  assert(s.beginMutationSnapshot(during));assert(s.mutationSnapshotUnchanged(during));
  assert(s.mkdir("dir",true));assert(!s.mutationSnapshotUnchanged(during));
  assert(s.beginMutationSnapshot(during));assert(s.rename("a","b"));assert(!s.mutationSnapshotUnchanged(during));
  assert(s.beginMutationSnapshot(during));assert(s.remove("file"));assert(!s.mutationSnapshotUnchanged(during));
  assert(s.beginMutationSnapshot(during));assert(s.replaceFile("a","b"));assert(!s.mutationSnapshotUnchanged(during));
  assert(s.beginMutationSnapshot(during));assert(s.rmdir("dir"));assert(!s.mutationSnapshotUnchanged(during));
  assert(s.beginMutationSnapshot(during));assert(s.removeDir("dir"));assert(!s.mutationSnapshotUnchanged(during));
 }else if(which=="checked-read-alloc-fails"){
  failAllocation=true;auto reader=s.openChecked("read",O_RDONLY);assert(!reader);
  assert(s.mutationSnapshotUnchanged(before));
 }else if(which=="checked-write-alloc-fails"){
  failAllocation=true;auto writer=s.openChecked("write",O_WRONLY|O_CREAT);assert(!writer);
  assert(s.mutationSnapshotUnchanged(before));
 }else if(which=="checked-writer"){
  auto writer=s.openChecked("write",O_WRONLY|O_CREAT);HalStorage::MutationSnapshot next;
  assert(writer && !s.beginMutationSnapshot(next));assert(writer.close());assert(s.beginMutationSnapshot(next));
 }else if(which=="failed-open"){
  SDCard.failOpen=true;HalFile writer;assert(!s.openFileForWrite("TEST","bad",writer));
  assert(!s.mutationSnapshotUnchanged(before));HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="open-fails"){
  SDCard.failOpen=true;auto writer=s.open("bad",O_WRONLY|O_TRUNC);
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="truncate-fails"){
  auto writer=s.open("write",O_WRONLY);writer.impl->file.state->truncateOk=false;
  assert(!writer.truncate(0));assert(writer.close());HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="rename-fails"){
  auto reader=s.open("read",O_RDONLY);reader.impl->file.state->renameOk=false;
  assert(!reader.rename("new"));assert(reader.close());HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="replace"){
  HalFile file;assert(s.openFileForWrite("TEST","write",file));
  HalStorage::MutationSnapshot during;assert(!s.beginMutationSnapshot(during));
  assert(s.openFileForRead("TEST","read",file));assert(s.beginMutationSnapshot(during));
 }else if(which=="close-fails"){
  SDCard.nextCloseOk=false;auto writer=s.open("write",O_WRONLY);assert(!writer.close());
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="flush-fails"){
  auto writer=s.open("write",O_WRONLY);writer.impl->file.state->syncOk=false;
  writer.flush();writer.impl->file.state->syncOk=true;assert(writer.close());
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="sync-fails"){
  auto writer=s.open("write",O_WRONLY);writer.impl->file.state->syncOk=false;
  assert(!writer.sync());writer.impl->file.state->syncOk=true;assert(writer.close());
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="record-read"){
  auto dir=s.open("read",O_RDONLY);dir.impl->file.state->directory=true;uint8_t bytes[32]{};
  assert(s.readDirectoryRecord(dir,before,bytes)==HalStorage::SnapshotRead::Record);
  assert(dir.impl->file.state->readCount==1);
  dir.impl->file.state->readResult=0;
  assert(s.readDirectoryRecord(dir,before,bytes)==HalStorage::SnapshotRead::End);
  assert(s.readDirectoryRecord(dir,before,nullptr)==HalStorage::SnapshotRead::Failed);
  assert(s.remove("other"));
  assert(s.readDirectoryRecord(dir,before,bytes)==HalStorage::SnapshotRead::Changed);
  assert(dir.impl->file.state->readCount==2);assert(dir.close());
 }else if(which=="record-fail"){
  auto dir=s.open("read",O_RDONLY);dir.impl->file.state->directory=true;dir.impl->file.state->readResult=4;
  uint8_t bytes[32]{};assert(s.readDirectoryRecord(dir,before,bytes)==HalStorage::SnapshotRead::Failed);
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));assert(dir.close());
 }else if(which=="verified-noops"){
  SDCard.pathExists=false;s.checkedStatus=HalStorage::PathStatus::Missing;
  assert(!s.remove("missing"));assert(!s.rmdir("missing"));assert(!s.removeDir("missing"));
  assert(s.mutationSnapshotUnchanged(before));
  SDCard.pathExists=true;s.checkedStatus=HalStorage::PathStatus::Present;
  assert(!s.mkdir("existing",true));assert(s.mutationSnapshotUnchanged(before));
 }else if(which=="uncertain-remove"){
  SDCard.pathExists=false;s.checkedStatus=HalStorage::PathStatus::Failed;SDCard.failMutation=true;
  assert(!s.remove("unknown"));assert(!s.mutationSnapshotUnchanged(before));
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="monitor-fault"){
  monitorFault=true;assert(!s.mutationSnapshotUnchanged(before));
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="short-write"){
  auto writer=s.open("write",O_WRONLY);writer.impl->file.state->shortWrite=true;
  const uint8_t data[]={1,2};assert(writer.write(data,2)==1);assert(writer.close());
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="write-error"){
  auto writer=s.open("write",O_WRONLY);writer.impl->file.state->writeError=true;assert(writer.close());
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="sdk-write-fails"){
  SDCard.failSdkWrite=true;assert(!s.writeFile("x","text"));
  HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
 }else if(which=="usb"){
  assert(s.beginUsbDrive());SDCard.mounted=true;HalStorage::MutationSnapshot next;
  assert(!s.mutationSnapshotUnchanged(before));assert(!s.beginMutationSnapshot(next));
 }else if(which=="usb-failed"){
  SDCard.canDetach=false;assert(!s.beginUsbDrive());HalStorage::MutationSnapshot next;
  assert(!s.mutationSnapshotUnchanged(before));assert(!s.beginMutationSnapshot(next));
 }else if(which=="unmount"){
  SDCard.mounted=false;HalStorage::MutationSnapshot next;assert(!s.beginMutationSnapshot(next));
  SDCard.mounted=true;assert(!s.mutationSnapshotUnchanged(before));assert(!s.beginMutationSnapshot(next));
 }else if(which=="thread"){
  std::thread t([&]{auto writer=s.open("write",O_WRONLY);assert(writer.write(uint8_t{1})==1);assert(writer.close());});
  t.join();assert(!s.mutationSnapshotUnchanged(before));
 }else return 2;
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-pthread', *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-I', str(root / 'lib/hal'), str(path / 'test.cpp'), '-o', str(path / 'test')]
    subprocess.run(command, check=True)
    for scenario in ['normal', 'checked-read-alloc-fails', 'checked-write-alloc-fails', 'checked-writer', 'failed-open', 'open-fails', 'truncate-fails', 'rename-fails', 'replace', 'close-fails', 'flush-fails', 'sync-fails', 'record-read', 'record-fail', 'verified-noops', 'uncertain-remove', 'monitor-fault', 'short-write', 'write-error', 'sdk-write-fails', 'usb', 'usb-failed', 'unmount', 'thread']:
        subprocess.run([str(path / 'test'), scenario], check=True)
print('HAL fence hooks preserve writer lifetime, fail closed, and invalidate on writes')
