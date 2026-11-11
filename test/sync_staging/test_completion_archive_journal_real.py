"""Compose production archive scanning and journal proof on FAT/exFAT images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preview_real', root / 'test/sync_staging/test_preview_real.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
code = fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
old = 'bool withStorageLock(bool (*)(void*),void*);'
assert old in code
code = code.replace(old, old + 'bool lookupNumberedChildren(const char*,const char*,uint64_t&);bool openFileForRead(const char*,const char*,HalFile&);')
old = 'bool close();bool isOpen()const;bool hasError()const;'
assert old in code
code = code.replace(old, 'size_t fileSize();' + old)
hal = fixture.fixture.fixture.hal
code += '\n' + hal.extract('bool HalStorage::lookupNumberedChildren(')
code += r'''
size_t HalFile::fileSize(){return static_cast<size_t>(fileSize64());}
bool HalStorage::openFileForRead(const char*,const char* path,HalFile& file){file=open(path,O_RDONLY);return bool(file);}
#define Storage HalStorage::getInstance()
#include "CompletionJournalIO.h"
#include "CompletionArchiveInventory.h"
'''
code += '\n' + (root / 'src/activities/reader/CompletionArchiveInventory.cpp').read_text()
journal = (root / 'src/activities/reader/CompletionJournalIO.cpp').read_text()
def part(start, end):
    return journal[journal.index(start):journal.index(end, journal.index(start))]
code += '\nnamespace readingstats::completionjournal::integration {\n'
code += 'bool validSnapshotPayload(const uint8_t*,const uint8_t*){assert(false);return false;}\n'
code += part('struct InspectContext {', 'struct SealContext {')
code += part('struct InspectSealContext {', 'enum class Comparison {')
code += part('struct DeferredProofContext {', 'struct StartContext {')
code += part('DeferredRetirementProof retiredDeferredMoveProof(', '\nStartResult start(')
code += '\n}\n'
code += r'''
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
 assert(volume.mkdir("/.completion-history-000002",true));
 if(scenario=="cross-duplicate")assert(volume.mkdir("/.completion-history-000000",true));
 const auto write=[&](const char* path,const uint8_t* bytes,size_t size){auto f=volume.open(path,O_CREAT|O_EXCL|O_WRONLY);
  assert(f && f.write(bytes,size)==size && f.sync() && f.close());};
 constexpr char source[]="/Authors/Writer/Book.epub";constexpr uint64_t origin=0x12345678;
 namespace cj=readingstats::completionjournal;
 cj::Book book{};cj::Global global{};book[0]=5;book[11]=1;global[0]=3;
 cj::Record move{}, other{};size_t moveSize=0,otherSize=0;
 assert(cj::encode("/.crosspoint/reading_stats/epub_123",book,global,{},move,moveSize,
                   cj::MOVE_TO_READ|cj::MOVE_FROM_DEFERRED,source,origin));
 assert(cj::encode("/.crosspoint/reading_stats/epub_123",book,global,{},other,otherSize));
 constexpr uint8_t marker[]={'C','P','J','A',1,0,0,0};
 for(unsigned archive : {2u,0u}) {if(archive==0 && scenario!="cross-duplicate")continue;
  char markerPath[96];std::snprintf(markerPath,sizeof(markerPath),"/.completion-history-%06u/.archive-00",archive);
  write(markerPath,marker,sizeof(marker));
 for(unsigned slot=0;slot<64;++slot){const bool wanted=slot==0 || (scenario=="duplicate" && slot==1);
  const auto& record=wanted?move:other;const size_t size=wanted?moveSize:otherSize;
  char path[96];std::snprintf(path,sizeof(path),"/.completion-history-%06u/.completion-%02u",archive,slot);write(path,record.data(),size);
  cj::Seal seal{},retirement{};assert(cj::encodeSeal(slot,record,size,seal) && cj::encodeRetirement(slot,record,size,retirement));
  std::snprintf(path,sizeof(path),"/.completion-history-%06u/.cseal-%02u",archive,slot);write(path,seal.data(),seal.size());
  std::snprintf(path,sizeof(path),"/.completion-history-%06u/.cretire-%02u",archive,slot);write(path,retirement.data(),retirement.size());
 }}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 cj::RetiredJournalLocation location{};location.slot=99;location.directory[0]='x';const int writes=disk.writes;
 const auto result=cj::integration::retiredDeferredMoveProof(source,origin,cj::checksum(move.data(),moveSize),&location);
 if(disk.injectedRead)assert(result!=cj::DeferredRetirementProof::Retired && location.slot==99 && location.directory[0]=='x');
 else if(scenario=="duplicate" || scenario=="cross-duplicate")assert(result==cj::DeferredRetirementProof::Conflict && location.slot==99);
 else {assert(result==cj::DeferredRetirementProof::Retired && location.slot==0);
  assert(std::string(location.directory.data())=="/.completion-history-000002");}
 assert(disk.writes==writes);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injectedRead?"injected":"not reached");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HalStorage.h').write_text('#pragma once\n')
    (tmp / 'Memory.h').write_text('#pragma once\n')
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    cmd = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
           '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp), '-I', str(root / 'lib/hal'), '-I', str(hal.dependency / 'src'),
           '-I', str(root / 'src/network/sync'), '-I', str(root / 'src'), '-I', str(root / 'test/sync_staging/stubs'),
           '-I', str(root / 'src/activities/reader'), '-I', str(root / 'lib/FsHelpers'), '-I', str(root / 'lib/Serialization'),
           str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(cmd, check=True)
    for fs in ('fat', 'exfat'):
        for scenario in ('clear', 'duplicate', 'cross-duplicate', *(f'read-{n}' for n in range(24)),
                         'read-64', 'read-128', 'read-256'):
            subprocess.run([str(tmp / 'test'), fs, scenario], check=True)
print('Archive scanner and retired journal proof passed FAT/exFAT fixtures')
