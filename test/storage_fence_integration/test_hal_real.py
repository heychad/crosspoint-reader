"""Exercise selected actual HAL snapshot hooks with patched SdFat disk images."""
from pathlib import Path
import hashlib
import importlib.util
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/hal/HalStorage.cpp').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)

impl = source[source.index('class HalFile::Impl {'):source.index('\nHalFile::HalFile() = default;')]
constructors = source[source.index('HalFile::HalFile() = default;'):source.index('\nHalStorage::SnapshotRead HalStorage::readDirectoryRecord(')]
signatures = ('bool HalStorage::observedSdmmcHealthy()', 'bool HalStorage::beginMutationSnapshot(',
              'bool HalStorage::mutationSnapshotUnchanged(', 'HalStorage::SnapshotFormat HalStorage::snapshotFormat(',
              'bool HalStorage::captureMutationMarker(', 'bool HalStorage::mutationMarkerUnchanged(',
              'HalStorage::SnapshotRead HalStorage::readDirectoryRecord(',
              'HalStorage::SnapshotRead HalStorage::readDirectoryRecordAtPath(',
              'HalFile HalStorage::open(', 'HalFile HalStorage::openChecked(',
              'bool HalStorage::openFileForWrite(const char* moduleName, const char* path,',
              'uint32_t HalFile::creationTime()', 'uint32_t HalFile::modificationTime()', 'void HalFile::flush()', 'bool HalFile::sync()', 'size_t HalFile::write(const uint8_t*',
              'size_t HalFile::write(const void*', 'size_t HalFile::write(uint8_t', 'bool HalFile::close()',
              'bool HalFile::isOpen() const', 'HalFile::operator bool() const')
methods = '\n'.join(extract(signature) for signature in signatures)
real = (ROOT / 'test/completion_journal_real/CompletionJournalRealTest.cpp').read_text()
start = real.index('struct Disk : FsBlockDeviceInterface {')
end = real.index('\n};', start) + 3
disk = real[start:end].replace('injected = true;', 'injected = true; monitorFault = true;').replace(
    'injectedRead = true;', 'injectedRead = true; monitorFault = true;')
code = r'''
#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsVolume.h>
#include <FsLib/FsFile.h>
#include <common/FsBlockDeviceInterface.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include "StorageMutationFence.h"
#define FREEINK_DEVICE_X4PRO 1
bool monitorFault=false;
extern "C" bool crosspoint_sdmmc_monitor_healthy(){return !monitorFault;}
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... args){return std::make_unique<T>(std::forward<A>(args)...);}
''' + disk + r'''
struct Card {
 FsVolume* volume=nullptr;bool ready()const{return volume!=nullptr;}
 int opens=0;
 uint8_t filesystemType(){return volume?volume->fatType():0;}
 FsFile open(const char* path,oflag_t flags=O_RDONLY){++opens;return volume->open(path,flags);}
 bool openFileForWrite(const char*,const char* path,FsFile& file){file=volume->open(path,O_CREAT|O_TRUNC|O_WRONLY);return bool(file);}
} SDCard;
class HalFile {
 public:
 class Impl;std::unique_ptr<Impl> impl;
 HalFile();explicit HalFile(std::unique_ptr<Impl>);~HalFile();HalFile(HalFile&&);HalFile& operator=(HalFile&&);
 void flush();bool sync();size_t write(const uint8_t*,size_t);size_t write(const void*,size_t);size_t write(uint8_t);
 uint32_t creationTime();uint32_t modificationTime();
 bool close();bool isOpen()const;explicit operator bool()const;
};
class HalStorage {
 friend class HalFile;
 checkedscan::StorageMutationFence mutationFence;
 public:
 using MutationSnapshot=checkedscan::StorageMutationFence::Snapshot;
 using MutationMarker=checkedscan::StorageMutationFence::Marker;
 enum class SnapshotRead {Record,End,Changed,Failed};
 enum class PathStatus {Missing,Present,Failed};
 struct DirectoryCursor {MutationSnapshot snapshot{};uint64_t position=0;uint32_t cluster=0;bool started=false,ended=false,failed=false;uint8_t buffered=0,nextRecord=0,records[8][32]{};char path[2049]{};};
 PathStatus lookupPath(const char* path){auto file=SDCard.open(path);if(!file)return PathStatus::Missing;return file.close()?PathStatus::Present:PathStatus::Failed;}
 enum class SnapshotFormat {Unknown,Fat,ExFat};
 class StorageLock {static std::recursive_mutex mutex;std::lock_guard<std::recursive_mutex> guard;
 public:StorageLock():guard(mutex){}};
 static HalStorage& getInstance(){static HalStorage instance;return instance;}
 bool observedSdmmcHealthy()const;bool beginMutationSnapshot(MutationSnapshot&);bool mutationSnapshotUnchanged(const MutationSnapshot&);
 bool captureMutationMarker(MutationMarker&);bool mutationMarkerUnchanged(const MutationMarker&);
 SnapshotFormat snapshotFormat(const MutationSnapshot&);
 SnapshotRead readDirectoryRecord(HalFile&,const MutationSnapshot&,uint8_t*);
 SnapshotRead readDirectoryRecordAtPath(const char*,const MutationSnapshot&,DirectoryCursor&,uint8_t*);
 HalFile open(const char*,oflag_t);HalFile openChecked(const char*,oflag_t);
 bool openFileForWrite(const char*,const char*,HalFile&);
};
std::recursive_mutex HalStorage::StorageLock::mutex;
''' + impl + '\n' + constructors + '\n' + methods + r'''
#include "StorageDirectoryScan.h"
#include "PathDirectoryScan.h"
#include "InventorySession.h"
#include "AutomaticBackupRetentionScan.h"
static_assert(sizeof(wirecheck::InventorySession) <= 8192, "Inventory session grew beyond its host-tested heap budget");
struct ScanContext {int entries=0;};
bool keepGoing(void*){return true;}
bool emitEntry(void* raw,const wirecheck::DirectoryEntry&){++static_cast<ScanContext*>(raw)->entries;return true;}
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat", failure=std::string(argv[2])=="failure";
 Disk disk;uint8_t scratch[512];
 if(exfat){ExFatFormatter formatter;assert(formatter.format(&disk,scratch));}
 else {FatFormatter formatter;assert(formatter.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/books",true));
 {auto progress=volume.open("/books/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t saved[]={12,34,56,78};
  assert(progress && progress.write(saved,sizeof(saved))==sizeof(saved) && progress.sync() && progress.close());}
 if(std::string(argv[2]).starts_with("retention")){
  assert(volume.mkdir("/.crossink-stats-backup",true));
  for(int day=1;day<=10;++day){char path[128];std::snprintf(path,sizeof(path),
   "/.crossink-stats-backup/stats_cp_202611%02d_A_0000000000000001.bin",day);
   auto f=volume.open(path,O_CREAT|O_EXCL|O_WRONLY);
   if(!f){std::fprintf(stderr,"cannot create %s, exfat=%d\n",path,exfat);std::abort();}
   assert(f.write(uint8_t{1})==1 && f.sync() && f.close());}
  auto manual=volume.open("/.crossink-stats-backup/stats_2026-11-10.bin",O_CREAT|O_EXCL|O_WRONLY);
  assert(manual && manual.write(uint8_t{2})==1 && manual.sync() && manual.close());
  assert(volume.mkdir("/.crossink-stats-backup/stats_cp_20261100_A_0000000000000001.bin",true));
 }
 {auto file=volume.open("/books/progress.bin",O_RDWR);
  assert(file && file.timestamp(T_CREATE,2020,1,2,3,4,6) && file.timestamp(T_WRITE,2025,6,7,8,9,10));
  assert(file.sync() && file.close());}
 auto& storage=HalStorage::getInstance();HalStorage::MutationSnapshot before;
 assert(storage.beginMutationSnapshot(before));
 {const int writes=disk.writes;auto file=storage.open("/books/progress.bin",O_RDONLY);assert(file);
  assert(file.creationTime()==((uint32_t(FS_DATE(2020,1,2))<<16)|FS_TIME(3,4,6)));
  assert(file.modificationTime()==((uint32_t(FS_DATE(2025,6,7))<<16)|FS_TIME(8,9,10)));
  assert(file.close());assert(file.creationTime()==0 && file.modificationTime()==0);
  assert(disk.writes==writes && storage.mutationSnapshotUnchanged(before));}

 assert(storage.beginMutationSnapshot(before));
 if(std::string(argv[2]).starts_with("retention")){
  constexpr char current[]="stats_cp_20261110_A_0000000000000001.bin";
  if(std::string(argv[2]).starts_with("retention-readfault-")){
   SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
   const int offset=std::stoi(std::string(argv[2]).substr(20));
   disk.failReadAt=disk.reads+offset;const int writesBefore=disk.writes;
   auto scan=std::make_unique<statsbackup::AutomaticBackupRetentionScan>(storage,before,current);
   auto result=wirecheck::ScanResult::Pending;
   for(int n=0;n<2000 && result==wirecheck::ScanResult::Pending;++n)result=scan->step();
   assert(result!=wirecheck::ScanResult::Pending && disk.writes==writesBefore);
   if(disk.injectedRead)assert(result==wirecheck::ScanResult::Failed && !scan->candidate());
   else assert(result==wirecheck::ScanResult::Complete && scan->candidate());
   std::printf("%s retention read fault %d: %s\n",exfat?"exfat":"fat",offset,disk.injectedRead?"injected":"not reached");
   return 0;
  }
  auto scan=std::make_unique<statsbackup::AutomaticBackupRetentionScan>(storage,before,current);
  if(std::string(argv[2])=="retention-cancel"){
   assert(scan->step()==wirecheck::ScanResult::Pending);scan->cancel();assert(!scan->candidate());return 0;
  }
  if(std::string(argv[2])=="retention-change"){
   assert(scan->step()==wirecheck::ScanResult::Pending);
   auto changed=storage.openChecked("/books/changed",O_CREAT|O_EXCL|O_WRONLY);assert(changed);
   assert(changed.write(uint8_t{1})==1 && changed.sync() && changed.close());
   assert(scan->step()==wirecheck::ScanResult::Failed && !scan->candidate());return 0;
  }
  auto result=wirecheck::ScanResult::Pending;
  for(int n=0;n<2000 && result==wirecheck::ScanResult::Pending;++n)result=scan->step();
  assert(result==wirecheck::ScanResult::Complete && scan->count()==10);
  assert(scan->candidate() && std::strcmp(scan->candidate(),"stats_cp_20261101_A_0000000000000001.bin")==0);
  auto changed=storage.openChecked("/books/changed",O_CREAT|O_EXCL|O_WRONLY);assert(changed);
  assert(changed.write(uint8_t{1})==1 && changed.sync() && changed.close());
  assert(!scan->candidate());return 0;
 }
 if(std::string(argv[2]).find("readfault-")==0){
  SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
  const int offset=std::stoi(std::string(argv[2]).substr(10));
  disk.failReadAt=disk.reads+offset;const int writesBefore=disk.writes;
  ScanContext ctx;wirecheck::PathDirectoryScan scan(storage,before,"/books",{&ctx,keepGoing,emitEntry});
  auto result=wirecheck::ScanResult::Pending;
  for(int steps=0;result==wirecheck::ScanResult::Pending && steps<1000;++steps)result=scan.step();
  scan.cancel();assert(result!=wirecheck::ScanResult::Pending);
  if(disk.injectedRead){assert(result==wirecheck::ScanResult::Failed);assert(!storage.mutationSnapshotUnchanged(before));}
  else assert(result==wirecheck::ScanResult::Complete);
  std::printf("%s read fault offset %d: %s\n",exfat?"exfat":"fat",offset,disk.injectedRead?"injected":"not reached");
  assert(disk.writes==writesBefore);
  disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
  auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t state[4]{};
  assert(progress && progress.read(state,sizeof(state))==sizeof(state) && progress.close());
  assert(state[0]==12 && state[1]==34 && state[2]==56 && state[3]==78);return 0;
 }
 if(std::string(argv[2])=="pathfailure"){
  ScanContext ctx;wirecheck::PathDirectoryScan scan(storage,before,"/books",{&ctx,keepGoing,emitEntry});
  assert(scan.step()==wirecheck::ScanResult::Pending);
  auto dirty=storage.openChecked("/books/dirty.epub",O_CREAT|O_EXCL|O_WRONLY);assert(dirty);
  const uint8_t byte=7;assert(dirty.write(byte)==1);disk.failAt=disk.writes;assert(!dirty.sync() && disk.injected);
  disk.failAt=-1;const int beforeClose=disk.writes;
  assert(scan.step()==wirecheck::ScanResult::Failed);scan.cancel();assert(disk.writes==beforeClose);
  dirty.close();SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
  auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t state[4]{};
  assert(progress && progress.read(state,sizeof(state))==sizeof(state) && progress.close());
  assert(state[0]==12 && state[1]==34 && state[2]==56 && state[3]==78);return 0;
 }
 auto directory=storage.openChecked("/books",O_RDONLY);assert(directory);
 auto writer=storage.openChecked("/books/test.epub",O_CREAT|O_EXCL|O_WRONLY);assert(writer);
 assert(!storage.mutationSnapshotUnchanged(before));HalStorage::MutationSnapshot after;
 assert(!storage.beginMutationSnapshot(after));HalStorage::MutationMarker marker;assert(storage.captureMutationMarker(marker));
 const uint8_t bytes[]={3,7,9};assert(writer.write(bytes,sizeof(bytes))==sizeof(bytes));
 assert(!storage.mutationMarkerUnchanged(marker));assert(storage.captureMutationMarker(marker));
 if(failure){disk.failAt=disk.writes;assert(!writer.sync());assert(disk.injected);disk.failAt=-1;writer.close();
  assert(!storage.beginMutationSnapshot(after));assert(!storage.mutationSnapshotUnchanged(before));assert(directory.close());
  SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
  auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t state[4]{};
  assert(progress && progress.read(state,sizeof(state))==sizeof(state) && progress.close());
  assert(state[0]==12 && state[1]==34 && state[2]==56 && state[3]==78);
  return 0;}
 assert(writer.sync() && writer.close());assert(!storage.mutationMarkerUnchanged(marker));assert(storage.beginMutationSnapshot(after));
 uint8_t entry[32]{};assert(storage.readDirectoryRecord(directory,before,entry)==HalStorage::SnapshotRead::Changed);
 assert(storage.readDirectoryRecord(directory,after,entry)==HalStorage::SnapshotRead::Record);
 assert(directory.close());assert(storage.mutationSnapshotUnchanged(after));
 assert(storage.snapshotFormat(after)==(exfat?HalStorage::SnapshotFormat::ExFat:HalStorage::SnapshotFormat::Fat));
 {
  assert(!wirecheck::InventorySession::includedName(".private"));
  assert(!wirecheck::InventorySession::includedName("sYsTeM vOlUmE iNfOrMaTiOn"));
  assert(!wirecheck::InventorySession::includedName("xtcache"));
  assert(wirecheck::InventorySession::includedName("Books"));
  ScanContext ctx;auto session=std::make_unique<wirecheck::InventorySession>(storage,wirecheck::InventorySession::Callbacks{&ctx,keepGoing,emitEntry});
  assert(session->snapshotValid() && session->beginDirectory("/books"));
  auto state=wirecheck::ScanResult::Pending;
  while(state==wirecheck::ScanResult::Pending)state=session->step();
  assert(state==wirecheck::ScanResult::Complete && ctx.entries>=2 && session->snapshotValid());
  assert(session->beginDirectory("/"));state=wirecheck::ScanResult::Pending;
  while(state==wirecheck::ScanResult::Pending)state=session->step();
  assert(state==wirecheck::ScanResult::Complete && session->snapshotValid());
 }
 for(const char* bad : {"/books/../file", "/books/", "/.hidden", "/books/ABC~1"}) {
  ScanContext ctx;auto session=std::make_unique<wirecheck::InventorySession>(storage,wirecheck::InventorySession::Callbacks{&ctx,keepGoing,emitEntry});
  assert(!session->beginDirectory(bad) && !session->snapshotValid());
 }
 {
  ScanContext ctx;auto session=std::make_unique<wirecheck::InventorySession>(storage,wirecheck::InventorySession::Callbacks{&ctx,keepGoing,emitEntry});
  assert(session->beginDirectory("/books") && session->step()==wirecheck::ScanResult::Pending);
  auto changing=storage.openChecked("/books/session-change.epub",O_CREAT|O_EXCL|O_WRONLY);assert(changing && changing.close());
  assert(session->step()==wirecheck::ScanResult::Failed && !session->snapshotValid());
 }
 assert(storage.beginMutationSnapshot(after));
 {
  HalStorage::DirectoryCursor cursor;uint8_t record[32]{};
  assert(storage.readDirectoryRecordAtPath("/books",after,cursor,record)==HalStorage::SnapshotRead::Record);
  assert(cursor.buffered>1);const int opensAfterBatch=SDCard.opens;
  assert(storage.readDirectoryRecordAtPath("/books",after,cursor,record)==HalStorage::SnapshotRead::Record);
  assert(SDCard.opens==opensAfterBatch);
  std::memset(record,0xa5,sizeof(record));
  assert(storage.readDirectoryRecordAtPath("/",after,cursor,record)==HalStorage::SnapshotRead::Failed);
  for(auto byte:record)assert(byte==0xa5);
 }
 {
  HalStorage::DirectoryCursor cursor;uint8_t record[32]{};bool marker=false;
  while(storage.readDirectoryRecordAtPath("/books",after,cursor,record)==HalStorage::SnapshotRead::Record) {
   if(record[0]==0){assert(cursor.nextRecord==cursor.buffered);marker=true;break;}
  }
  assert(marker);
 }
 {
  ScanContext ctx;wirecheck::PathDirectoryScan scan(storage,after,"/books",{&ctx,keepGoing,emitEntry});
  auto state=wirecheck::ScanResult::Pending;
  while(state==wirecheck::ScanResult::Pending)state=scan.step();
  assert(state==wirecheck::ScanResult::Complete && ctx.entries>=2);
 }
 {
  ScanContext ctx;wirecheck::PathDirectoryScan scan(storage,after,"/books",{&ctx,keepGoing,emitEntry});
  assert(scan.step()==wirecheck::ScanResult::Pending);
  auto changing=storage.openChecked("/books/path-change.epub",O_CREAT|O_EXCL|O_WRONLY);assert(changing && changing.close());
  assert(scan.step()==wirecheck::ScanResult::Failed);
 }
 assert(storage.beginMutationSnapshot(after));
 {
  auto scanDir=storage.openChecked("/books",O_RDONLY);assert(scanDir);ScanContext ctx;
  wirecheck::StorageDirectoryScan scan(storage,after,std::move(scanDir),{&ctx,keepGoing,emitEntry});
  auto state=wirecheck::ScanResult::Pending;
  while(state==wirecheck::ScanResult::Pending)state=scan.step();
  assert(state==wirecheck::ScanResult::Complete && ctx.entries>=2);
 }
 {
  auto scanDir=storage.openChecked("/books",O_RDONLY);assert(scanDir);ScanContext ctx;
  wirecheck::StorageDirectoryScan scan(storage,after,std::move(scanDir),{&ctx,keepGoing,emitEntry});
  assert(scan.step()==wirecheck::ScanResult::Pending);
  auto changing=storage.openChecked("/books/change.epub",O_CREAT|O_EXCL|O_WRONLY);assert(changing && changing.close());
  assert(scan.step()==wirecheck::ScanResult::Failed);
 }
 assert(storage.beginMutationSnapshot(after));
 HalFile viaWrite;assert(storage.openFileForWrite("TEST","/books/second.epub",viaWrite));
 assert(!storage.mutationSnapshotUnchanged(after));assert(!storage.beginMutationSnapshot(before));
 assert(viaWrite.close());assert(storage.beginMutationSnapshot(before));return 0;
}
'''
dependency = ROOT / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('sdfat_patch', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
for _, relative, _, expected in patcher.PATCHES:
    if hashlib.sha256((dependency / relative).read_bytes()).hexdigest() != expected:
        raise RuntimeError('SdFat dependency does not match the pinned patched source: ' + relative)
files = []
for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common'):
    files.extend(sorted((dependency / 'src' / folder).glob('*.cpp')))
flags = ['-std=c++20', '-O1', '-pthread', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
         '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
         '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
         '-DDESTRUCTOR_CLOSES_FILE=1', '-DUSE_SEPARATE_FAT_CACHE=1']
if __name__ == '__main__':
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        (tmp / 'test.cpp').write_text(code)
        (tmp / 'HalStorage.h').write_text('#pragma once\n')
        # SdFat's FAT short-name collision resolver mixes in millis(). A frozen
        # clock cannot create more than two long filenames sharing a prefix.
        (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long tick=1;return tick++;}\n')
        command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
                   '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp), '-I', str(ROOT / 'lib/hal'),
                   '-I', str(dependency / 'src'), '-I', str(ROOT / 'src/network/sync'), '-I', str(ROOT / 'src/activities/reader'), '-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/FsHelpers'), str(tmp / 'test.cpp'), *map(str, files), '-o', str(tmp / 'test')]
        subprocess.run(command, check=True)
        for filesystem in ('fat', 'exfat'):
            for scenario in ('normal', 'failure', 'pathfailure', 'retention', 'retention-change', 'retention-cancel', *(f'retention-readfault-{n}' for n in range(12)), *(f'readfault-{n}' for n in range(12))):
                subprocess.run([str(tmp / 'test'), filesystem, scenario], check=True)
    print('Selected production HAL snapshot hooks passed on FAT and exFAT SdFat images')
