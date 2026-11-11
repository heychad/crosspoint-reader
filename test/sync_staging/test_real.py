"""Exercise staging with selected production HAL and real SdFat images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('hal_fixture', root / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
code = '#include <vector>\n' + hal.code.replace('bool close();bool isOpen()const;explicit operator bool()const;',
                        'bool close();bool isOpen()const;bool hasError()const;bool isDirectory()const;uint64_t fileSize64();int read(void*,size_t);explicit operator bool()const;')
stub = 'PathStatus lookupPath(const char* path){auto file=SDCard.open(path);if(!file)return PathStatus::Missing;return file.close()?PathStatus::Present:PathStatus::Failed;}'
assert stub in code
code = code.replace(stub, 'PathStatus lookupPath(const char* path);bool withStorageLock(bool (*)(void*),void*);')
code = code.replace('#include "StorageDirectoryScan.h"',
                    '#include "CheckedStoragePath.h"\n' + hal.extract('HalStorage::PathStatus HalStorage::lookupPath(')
                    + '\n' + hal.extract('bool HalStorage::withStorageLock(')
                    + '\n#define HAL_FILE_WRAPPED_CALL(method, ...) HalStorage::StorageLock lock; assert(impl); return impl->file.method(__VA_ARGS__);\n'
                    + '#define HAL_FILE_FORWARD_CALL(method, ...) assert(impl); return impl->file.method(__VA_ARGS__);\n'
                    + '\n'.join(hal.extract(signature) for signature in ('bool HalFile::hasError() const',
                       'bool HalFile::isDirectory() const', 'uint64_t HalFile::fileSize64()', 'int HalFile::read(void*'))
                    + '\n#include "StageWriter.h"\n' + (root / 'src/network/sync/StageWriter.cpp').read_text()
                    + '\n#include "StageDigest.h"\n' + (root / 'src/network/sync/StageDigest.cpp').read_text()
                    + '\n#include "StorageDirectoryScan.h"')
needle = 'assert(storage.beginMutationSnapshot(before));\n if(std::string(argv[2]).starts_with("retention"))'
stage = r'''
assert(storage.beginMutationSnapshot(before));
if(std::string(argv[2]).starts_with("stage")){
 constexpr char name[]="__xsync_0123456789abcdef0123456789abcdef.part";
 constexpr char path[]="/__xsync_0123456789abcdef0123456789abcdef.part";
 std::vector<uint8_t> bytes(5000,42);StageWriter writer(storage);
 const bool writeFault=std::string(argv[2]).starts_with("stagefault-");
 const bool readFault=std::string(argv[2]).starts_with("stageread-");
 const bool hashFault=std::string(argv[2]).starts_with("stagehashread-");
 const bool fault=writeFault || readFault || hashFault;
 if(writeFault)disk.failAt=disk.writes+std::stoi(std::string(argv[2]).substr(11));
 if(readFault)disk.failReadAt=disk.reads+std::stoi(std::string(argv[2]).substr(10));
 const bool begun=writer.begin(name,bytes.size());
 const bool written=begun && writer.write(bytes.data(),bytes.size());
 const bool finished=written && writer.finish();
 const bool success=writer.takeResult();
 if(begun || disk.injected || disk.injectedRead) assert(!storage.mutationSnapshotUnchanged(before));
 if(disk.injected || disk.injectedRead) assert(!success);else assert(begun && written && finished && success);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injected || disk.injectedRead?"injected":"not reached");
 disk.failAt=-1;disk.failReadAt=-1;
 if(hashFault){
  SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
  disk.failReadAt=disk.reads+std::stoi(std::string(argv[2]).substr(14));
  uint8_t digest[32];std::memset(digest,0x5a,sizeof(digest));auto proceed=[](void*){return true;};
  const auto result=syncstage::hashStage(storage,name,bytes.size(),digest,proceed,nullptr);
  if(disk.injectedRead){assert(result!=syncstage::DigestResult::Ok);for(auto b:digest)assert(b==0x5a);}
  else assert(result==syncstage::DigestResult::Ok);
  std::printf("%s %s: %s\n",argv[1],argv[2],disk.injectedRead?"injected":"not reached");
  disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
  auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t saved[4]{};
  assert(progress && progress.read(saved,sizeof(saved))==sizeof(saved) && progress.close());
  assert(saved[0]==12 && saved[1]==34 && saved[2]==56 && saved[3]==78);return 0;
 }
 if(!fault){
  uint8_t digest[32], expected[32];CC_SHA256(bytes.data(),bytes.size(),expected);
  auto proceed=[](void*){return true;};
  assert(syncstage::hashStage(storage,name,bytes.size(),digest,proceed,nullptr)==syncstage::DigestResult::Ok);
  assert(std::memcmp(digest,expected,sizeof(digest))==0);
  auto mutate=[](void* raw){auto& s=*static_cast<HalStorage*>(raw);
    auto f=s.openChecked("/books/during-hash",O_CREAT|O_EXCL|O_WRONLY);assert(f && f.close());return true;};
  std::memset(digest,0x5a,sizeof(digest));
  assert(syncstage::hashStage(storage,name,bytes.size(),digest,mutate,&storage)==syncstage::DigestResult::Changed);
  for(auto b:digest)assert(b==0x5a);
 }
 if(!fault){HalStorage::MutationSnapshot current;assert(storage.beginMutationSnapshot(current));
  assert(!writer.begin(name,bytes.size()) && !writer.takeResult());
  assert(storage.mutationSnapshotUnchanged(current));}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 if(success){auto stage=volume.open(path,O_RDONLY);std::vector<uint8_t> actual(bytes.size());
  assert(stage && stage.read(actual.data(),actual.size())==static_cast<int>(actual.size()) && stage.close());
  assert(actual==bytes);}
 auto extra=volume.open("/books/later.bin",O_CREAT|O_EXCL|O_WRONLY);assert(extra);
 std::vector<uint8_t> allocation(32768,0x5a);
 assert(extra.write(allocation.data(),allocation.size())==allocation.size() && extra.sync() && extra.close());
 auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t saved[4]{};
 assert(progress && progress.read(saved,sizeof(saved))==sizeof(saved) && progress.close());
 assert(saved[0]==12 && saved[1]==34 && saved[2]==56 && saved[3]==78);return 0;
}
if(std::string(argv[2]).starts_with("retention"))'''
assert needle in code
code = code.replace(needle, stage)
def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        (tmp / 'test.cpp').write_text(code)
        (tmp / 'HalStorage.h').write_text('#pragma once\n')
        (tmp / 'Memory.h').write_text('#pragma once\n')
        (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
        cmd = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp), '-I', str(root / 'lib/hal'),
               '-I', str(hal.dependency / 'src'), '-I', str(root / 'src/network/sync'),
               '-I', str(root / 'src'), '-I', str(root / 'test/sync_staging/stubs'),
               '-I', str(root / 'src/activities/reader'), '-I', str(root / 'src'),
               '-I', str(root / 'lib/FsHelpers'), '-I', str(root / 'lib/Serialization'), str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
        subprocess.run(cmd, check=True)
        for filesystem in ('fat', 'exfat'):
            for scenario in ('stage', *(f'stagefault-{n}' for n in range(12)), *(f'stageread-{n}' for n in range(12)),
                             *(f'stagehashread-{n}' for n in range(12))):
                subprocess.run([str(tmp / 'test'), filesystem, scenario], check=True)
    print('Create-only stage preserved contents and reading state on FAT and exFAT images')

if __name__ == "__main__":
    main()
