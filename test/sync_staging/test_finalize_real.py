"""Exercise prototype publication on patched FAT/exFAT disk images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preview_real', root/'test/sync_staging/test_preview_real.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
hal = fixture.fixture.fixture.hal
code = fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code = code.replace('bool openFileForWrite(const char*,const char* path,FsFile& file)',
                    'bool rename(const char* a,const char* b){return volume->rename(a,b);}\n bool remove(const char* p){return volume->remove(p);}\n bool exists(const char* p){return volume->exists(p);}\n bool openFileForWrite(const char*,const char* path,FsFile& file)')
code = code.replace('bool withStorageLock(bool (*)(void*),void*);',
                    'bool withStorageLock(bool (*)(void*),void*);bool rename(const char*,const char*);bool remove(const char*);')
code += '\n#define HAL_STORAGE_MUTATION_CALL(method, ...) HalStorage::StorageLock lock; mutationFence.mutationAttempt(); const bool ok=SDCard.method(__VA_ARGS__); if(!ok)mutationFence.poison(); return ok;\n'
code += hal.extract('bool HalStorage::rename(')
code += hal.extract('bool HalStorage::remove(')
for name in ('DestinationGuard', 'CanonicalDestination', 'StoredReferenceGuard', 'FinalizePublication', 'ArchivePublication', 'CleanupPublication'):
    code += f'\n#include "{name}.h"\n' + (root/f'src/network/sync/{name}.cpp').read_text()
code += r'''
bool proceed(void*){return true;}
bool gate(const char*,void*){return true;}
bool acquire(void*){return true;} void release(void*){}
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/.crosspoint",true));assert(volume.mkdir("/Authors",true));
 {auto f=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};assert(f && f.write(p,4)==4 && f.sync() && f.close());}
 auto& storage=HalStorage::getInstance();constexpr char name[]="__xsync_0123456789abcdef0123456789abcdef.part";
 constexpr char path[]="/__xsync_0123456789abcdef0123456789abcdef.part";
 constexpr char copy[]="/.crosspoint/__xsync_0123456789abcdef0123456789abcdef.part-r1";
 constexpr char dest[]="/Authors/Book.epub";std::vector<uint8_t> bytes(5000,42);
 StageWriter writer(storage);assert(writer.begin(name,bytes.size()) && writer.write(bytes.data(),bytes.size()) && writer.finish() && writer.takeResult());
 {auto f=storage.openChecked(copy,O_CREAT|O_EXCL|O_WRONLY);assert(f && f.write(bytes.data(),bytes.size())==bytes.size() && f.sync() && f.close());}
 syncpublish::IntentView intent{name,dest,bytes.size(),{}};CC_SHA256(bytes.data(),bytes.size(),intent.digest.data());
 assert(syncpublish::prepareIntent(storage,intent)==syncpublish::PrepareResult::Prepared);
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("write-"))disk.failAt=disk.writes+std::stoi(scenario.substr(6));
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 const auto result=syncpublish::finalizeVerifiedCopy(storage,proceed,nullptr,gate,nullptr,acquire,release,nullptr);
 if(disk.injected || disk.injectedRead)assert(result!=syncpublish::FinalizeResult::Published);
 else assert(result==syncpublish::FinalizeResult::Published);
 syncpublish::ArchiveResult archived=syncpublish::ArchiveResult::Unavailable;
 if(result==syncpublish::FinalizeResult::Published && (scenario.starts_with("archive") || scenario.starts_with("cleanup"))){
  if(scenario.starts_with("archivewrite-"))disk.failAt=disk.writes+std::stoi(scenario.substr(13));
  archived=syncpublish::archiveVerifiedIntent(storage,name,proceed,nullptr);
  if(disk.injected || disk.injectedRead)assert(archived!=syncpublish::ArchiveResult::Archived);
  else assert(archived==syncpublish::ArchiveResult::Archived);
 }
 syncpublish::CleanupResult cleaned=syncpublish::CleanupResult::Unavailable;
 if(archived==syncpublish::ArchiveResult::Archived && scenario.starts_with("cleanup")){
  if(scenario.starts_with("cleanupwrite-"))disk.failAt=disk.writes+std::stoi(scenario.substr(13));
  if(scenario.starts_with("cleanupread-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(12));
  cleaned=syncpublish::cleanupArchivedStage(storage,name,proceed,nullptr);
  if(disk.injected || disk.injectedRead)assert(cleaned!=syncpublish::CleanupResult::Cleaned);
  else assert(cleaned==syncpublish::CleanupResult::Cleaned);
 }
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injected || disk.injectedRead?"injected":"not reached");
 disk.failAt=disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("cleanup")){auto extra=volume.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);
  std::vector<uint8_t> allocation(32768,0x5a);assert(extra && extra.write(allocation.data(),allocation.size())==allocation.size() && extra.sync() && extra.close());}
 auto stage=volume.open(path,O_RDONLY);std::vector<uint8_t> actual(bytes.size());
 if(stage && scenario.starts_with("cleanup")){assert(stage.close());}
 else if(stage)assert(stage.read(actual.data(),actual.size())==static_cast<int>(actual.size()) && stage.close() && actual==bytes);
 else assert(scenario.starts_with("cleanup"));
 auto progress=volume.open("/progress.bin",O_RDONLY);uint8_t p[4]{};assert(progress && progress.read(p,4)==4 && progress.close());
 assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
 constexpr char receipt[]="/.crosspoint/xsync-0123456789abcdef0123456789abcdef.done";
 assert(volume.exists(syncpublish::INTENT_PATH) || volume.exists(receipt));
 if(result==syncpublish::FinalizeResult::Published){auto published=volume.open(dest,O_RDONLY);std::vector<uint8_t> observed(bytes.size());
  assert(published && published.read(observed.data(),observed.size())==static_cast<int>(observed.size()) && published.close() && observed==bytes);
  if(archived==syncpublish::ArchiveResult::Archived && !disk.injected && !disk.injectedRead){const auto writes=disk.writes;
   assert(syncpublish::archiveVerifiedIntent(storage,name,proceed,nullptr)==syncpublish::ArchiveResult::AlreadyArchived);
   assert(disk.writes==writes);}
  else if(!scenario.starts_with("archive") && !scenario.starts_with("cleanup")){const auto writes=disk.writes;
   assert(syncpublish::finalizeVerifiedCopy(storage,proceed,nullptr,gate,nullptr,acquire,release,nullptr)==syncpublish::FinalizeResult::MatchingDestination);
   assert(disk.writes==writes);}}
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'test.cpp').write_text(code)
    (tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n')
    (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    cmd=[os.environ.get('CXX','c++'),*hal.flags,*shlex.split(os.environ.get('CXXFLAGS','')),
         '-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),'-I',str(hal.dependency/'src'),
         '-I',str(root/'src/network/sync'),'-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),
         '-I',str(root/'src/activities/reader'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),
         '-I',str(root/'.pio/libdeps/x4pro/ArduinoJson/src'),str(tmp/'test.cpp'),*map(str,hal.files),'-o',str(tmp/'test')]
    subprocess.run(cmd,check=True)
    for fs in ('fat','exfat'):
        for scenario in ('normal','archive','cleanup',*(f'write-{n}' for n in range(20)),*(f'read-{n}' for n in range(35)),*(f'archivewrite-{n}' for n in range(20)),*(f'cleanupwrite-{n}' for n in range(20)),*(f'cleanupread-{n}' for n in range(30))):
            subprocess.run([str(tmp/'test'),fs,scenario],check=True)
print('Prototype publication passed sampled FAT/exFAT fixtures')
