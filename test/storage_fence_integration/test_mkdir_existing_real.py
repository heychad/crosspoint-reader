"""Existing directories, including bookmark-style trailing slashes, are no-op failures."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('mkdir_hal', ROOT / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
code = hal.code[:hal.code.index('\n#include "StorageDirectoryScan.h"')]
code = code.replace('int opens=0;', '''int opens=0,mkdirCalls=0;
 bool exists(const char* path){return volume->exists(path);}
 bool mkdir(const char* path,bool parents){++mkdirCalls;return volume->mkdir(path,parents);}''')
lookup_stub = 'PathStatus lookupPath(const char* path){auto file=SDCard.open(path);if(!file)return PathStatus::Missing;return file.close()?PathStatus::Present:PathStatus::Failed;}'
assert lookup_stub in code
code = code.replace(lookup_stub, 'PathStatus lookupPath(const char*);bool mkdir(const char*,bool=true);')
code += '\n#include "CheckedStoragePath.h"\n' + hal.extract('HalStorage::PathStatus HalStorage::lookupPath(')
code += '\n' + hal.extract('bool HalStorage::mkdir(')
code += r'''
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";
 const std::string mode=argv[2];Disk disk;uint8_t scratch[512];
 if(exfat){ExFatFormatter formatter;assert(formatter.format(&disk,scratch));}
 else{FatFormatter formatter;assert(formatter.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
 assert(volume.mkdir("/.crosspoint/bookmarks",true));
 auto saved=volume.open("/.crosspoint/bookmarks/existing.json",O_WRONLY|O_CREAT|O_EXCL);
 const std::string bytes="saved bookmark and reading state";
 assert(saved&&saved.write(bytes.data(),bytes.size())==bytes.size()&&saved.sync()&&saved.close());
 auto& storage=HalStorage::getInstance();HalStorage::MutationSnapshot before,after;
 assert(storage.beginMutationSnapshot(before));
 if(mode=="existing"){
  const auto original=disk.sectors;
  // This is exactly the spelling returned by BookmarkUtil::getBookmarksDir().
  assert(!storage.mkdir("/.crosspoint/bookmarks/",true));
  assert(storage.beginMutationSnapshot(after)&&"existing bookmark directory must not poison storage observation");
  assert(storage.mutationSnapshotUnchanged(before)&&disk.sectors==original&&SDCard.mkdirCalls==0);
  for(const char* path:{"/.crosspoint/bookmarks","/.crosspoint/bookmarks///","/.crosspoint/bookmarks/existing.json"}){
   assert(!storage.mkdir(path,true));assert(storage.mutationSnapshotUnchanged(before));
   assert(disk.sectors==original&&SDCard.mkdirCalls==0);
  }
  // A new directory still counts as a mutation and admits a fresh snapshot.
  assert(storage.mkdir("/.crosspoint/new/nested",true)&&SDCard.mkdirCalls==1);
  assert(!storage.mutationSnapshotUnchanged(before)&&storage.beginMutationSnapshot(after));
  // Existing-directory no-ops do not hide an open writer.
  auto writer=storage.openChecked("/.crosspoint/new/open.tmp",O_WRONLY|O_CREAT|O_EXCL);
  assert(writer&&!storage.beginMutationSnapshot(before));
  assert(!storage.mkdir("/.crosspoint/bookmarks/",true)&&!storage.beginMutationSnapshot(before));
  assert(writer.close()&&storage.beginMutationSnapshot(before));
 }else if(mode=="writefault"){
  disk.failAt=disk.writes;disk.failStop=true;
  assert(!storage.mkdir("/.crosspoint/new",true));assert(disk.injected);
  assert(!storage.beginMutationSnapshot(after));
 }else if(mode=="driverfault"){
  monitorFault=true;assert(!storage.mkdir("/.crosspoint/bookmarks/",true));
  assert(!storage.beginMutationSnapshot(after));
 }else if(mode=="invalidparent"){
  assert(!storage.mkdir("/.crosspoint/bookmarks/existing.json/child",true));
  assert(!storage.beginMutationSnapshot(after));
 }else{assert(false);}
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / 'test.cpp').write_text(code)
    (directory / 'HostCompat.h').write_text('#include <cstdint>\nclass __FlashStringHelper;\ninline unsigned long millis(){static unsigned long now=0;return ++now;}\n')
    command = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(directory / 'HostCompat.h'), '-I', str(ROOT / 'lib/hal'),
               '-I', str(hal.dependency / 'src'), str(directory / 'test.cpp'), *map(str, hal.files),
               '-o', str(directory / 'test')]
    compiled = subprocess.run(command, capture_output=True, text=True)
    if compiled.returncode:
        raise AssertionError(compiled.stdout + compiled.stderr)
    for filesystem in ('fat', 'exfat'):
        for mode in ('existing', 'writefault', 'driverfault', 'invalidparent'):
            subprocess.run([str(directory / 'test'), filesystem, mode], check=True)
print('Production HAL mkdir preserves observation for existing paths; new writes, open writers, and real faults retain their gates')
