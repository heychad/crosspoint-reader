"""Exercise experimental legacy-bookmark publication on patched FAT/exFAT images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('preview_real', root / 'test/sync_staging/test_preview_real.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
hal = fixture.fixture.fixture.hal
code = fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code = code.replace('bool openFileForWrite(const char*,const char* path,FsFile& file)',
                    'bool rename(const char* a,const char* b){return volume->rename(a,b);}\n'
                    'bool remove(const char* p){return volume->remove(p);}\n'
                    'bool exists(const char* p){return volume->exists(p);}\n'
                    'bool openFileForWrite(const char*,const char* path,FsFile& file)')
code = code.replace('bool withStorageLock(bool (*)(void*),void*);',
                    'bool withStorageLock(bool (*)(void*),void*);bool rename(const char*,const char*);'
                    'bool remove(const char*);bool distinctMoveChains(const char*,const char*);')
code = code.replace('uint64_t fileSize64();int read(void*,size_t);',
                    'uint64_t fileSize64();size_t fileSize();bool seekSet(size_t);int read(void*,size_t);')
code += '\n#define HAL_STORAGE_MUTATION_CALL(method, ...) HalStorage::StorageLock lock; mutationFence.mutationAttempt(); const bool ok=SDCard.method(__VA_ARGS__); if(!ok)mutationFence.poison(); return ok;\n'
code += hal.extract('bool HalStorage::rename(') + '\n' + hal.extract('bool HalStorage::remove(')
code += '\n#define LOG_ERR(...) ((void)0)\n#define Storage HalStorage::getInstance()\n#include "SmallFileIdentity.h"\n' + hal.extract('bool HalStorage::distinctMoveChains(')
code += '\nsize_t HalFile::fileSize(){return impl->file.fileSize();}\n' + hal.extract('bool HalFile::seekSet(') + '\n'
code += (root / 'src/activities/reader/CrossInkBookmarkMove.cpp').read_text()
code += '\n' + (root / 'src/activities/reader/CrossInkBookmarkEdit.cpp').read_text()
if os.getenv('BOOKMARK_GUARD_DIAGNOSTICS'):
    code = code.replace('const bool copied = independent && copyOriginal(original, copy) && sameBytes(original, copy);',
                        'const bool wrote = independent && copyOriginal(original, copy); '
                        'const bool equal = wrote && sameBytes(original, copy); '
                        'std::fprintf(stderr,"guard independent=%d wrote=%d equal=%d\\n",independent,wrote,equal); '
                        'const bool copied = equal;')
    code = code.replace('const bool verified = original && copy && sameBytes(original, copy);',
                        'const bool verified = original && copy && sameBytes(original, copy); '
                        'std::fprintf(stderr,"guard reopened original=%d copy=%d verified=%d\\n",bool(original),bool(copy),verified);')
    code = code.replace('const bool finalInputClosed = input.close();',
                        'const bool finalInputClosed = input.close(); '
                        'std::fprintf(stderr,"final codec=%d complete=%d outputclose=%d inputclose=%d\\n",'
                        'int(result),complete,finalOutputClosed,finalInputClosed);')
    code = code.replace('  if (Storage.lookupPath(staged.c_str()) != HalStorage::PathStatus::Missing ||\n'
                        '      Storage.lookupPath(c.source.c_str()) != HalStorage::PathStatus::Present ||\n'
                        '      !Storage.distinctMoveChains(backup.c_str(), c.source.c_str())) {',
                        '  const auto ss=Storage.lookupPath(staged.c_str()); const auto ps=Storage.lookupPath(c.source.c_str()); '
                        'const bool distinct=Storage.distinctMoveChains(backup.c_str(), c.source.c_str()); '
                        'std::fprintf(stderr,"post stage=%d primary=%d distinct=%d\\n",int(ss),int(ps),distinct);\n'
                        '  if (ss != HalStorage::PathStatus::Missing || ps != HalStorage::PathStatus::Present || !distinct) {')
code += r'''
std::string readFile(FsVolume& volume,const char* path) {
 auto file=volume.open(path,O_RDONLY); if(!file)return {};
 std::string result(file.fileSize(),'\0'); int n=file.read(result.data(),result.size());
 if(n!=static_cast<int>(result.size()) || !file.close())return {}; return result;
}
void u32(std::string& bytes,uint32_t n){for(int i=0;i<4;++i)bytes.push_back(static_cast<char>(n>>(8*i)));}
void field(std::string& bytes,const std::string& value){u32(bytes,value.size());bytes+=value;}
int main(int argc,char** argv) {
 assert(argc==4); const bool exfat=std::string(argv[1])=="exfat";
 const std::string phase=argv[2]; const int fault=std::stoi(argv[3]);
 Disk disk; uint8_t scratch[512];
 if(exfat){ExFatFormatter formatter;assert(formatter.format(&disk,scratch));}
 else {FatFormatter formatter;assert(formatter.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk)); SDCard.volume=&volume;
 const std::string book="/Authors/Book.epub"; std::string original;
 original.push_back(5);original.push_back(2);original.push_back(0);
 field(original,"Title");field(original,"Author");field(original,book);
 original+=std::string(124,'A');original+=std::string(124,'B');
 std::string expected=original.substr(0,original.size()-248);expected[1]=1;expected+=std::string(124,'B');
 {auto file=volume.open("/bookmarks.bin",O_CREAT|O_EXCL|O_WRONLY);
  assert(file && file.write(original.data(),original.size())==original.size() && file.sync() && file.close());}
 {auto file=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(file && file.write(p,sizeof(p))==sizeof(p) && file.sync() && file.close());}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 using legacybookmarkedit::Result;
 const bool archive=phase.starts_with("archive"), publish=phase.starts_with("publish") || archive;
 const bool readFault=phase.ends_with("-read"), torn=phase.ends_with("-torn");
 if(publish)assert(legacybookmarkedit::stage("/bookmarks.bin",book,0,1)==Result::Ready);
 if(archive)assert(legacybookmarkedit::publish("/bookmarks.bin",book,0,1)==Result::Published);
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(fault>=0){if(readFault)disk.failReadAt=disk.reads+fault;else {disk.failStop=true;disk.failAt=disk.writes+fault;if(torn)disk.tornAfter=256;}}
 const auto result=archive?legacybookmarkedit::archivePublished("/bookmarks.bin",book,0,1)
                   :!publish?legacybookmarkedit::stage("/bookmarks.bin",book,0,1)
                            :legacybookmarkedit::publish("/bookmarks.bin",book,0,1);
 const bool injected=disk.injected || disk.injectedRead;
 disk.dead=false;disk.failAt=-1;disk.failReadAt=-1;monitorFault=false;SDCard.volume=nullptr;volume.end();
 assert(volume.begin(&disk));SDCard.volume=&volume;
 const auto primary=readFile(volume,"/bookmarks.bin");
 const auto backup=readFile(volume,"/bookmarks.bin.bookmark-edit.bak");
 const auto guard=readFile(volume,"/bookmarks.bin.bookmark-edit.guard");
 const auto archivedBackup=readFile(volume,"/bookmarks.bin.bookmark-edit.bak-0");
 const auto archivedGuard=readFile(volume,"/bookmarks.bin.bookmark-edit.guard-0");
 if(primary!=original && backup!=original && guard!=original && archivedBackup!=original && archivedGuard!=original){
  const auto stage=readFile(volume,"/bookmarks.bin.bookmark-edit.tmp");
  std::fprintf(stderr,"original lost: %s %s fault=%d injected=%d result=%d primary=%zu backup=%zu stage=%zu expected=%zu\n",
               argv[1],argv[2],fault,injected,static_cast<int>(result),primary.size(),backup.size(),stage.size(),original.size());return 4;
 }
 const auto progress=readFile(volume,"/progress.bin");assert(progress==std::string("\x0c\x22\x38\x4e",4));
 if(!injected && exfat && publish && !archive) {
  auto a=volume.open("/bookmarks.bin",O_RDONLY), b=volume.open("/bookmarks.bin.bookmark-edit.bak",O_RDONLY);
  assert(a && b && a.dirIndex()!=b.dirIndex() && a.firstCluster()!=b.firstCluster());
  assert((a.dirIndex() % 16)==(b.dirIndex() % 16));
  assert(!a.moveDifferentEntry(b));
  assert(moveFilesHaveIndependentStorage(a,b));
  assert(!moveFilesHaveIndependentStorage(a,a));
  assert(a.close() && b.close());
 }
 const auto success=archive?Result::Archived:!publish?Result::Ready:Result::Published;
 if(!injected && result!=success) {
  std::fprintf(stderr,"unexpected result: %s %s %d result=%d primary=%zu backup=%zu guard=%zu stage=%zu\n",
               argv[1],argv[2],fault,static_cast<int>(result),primary.size(),backup.size(),guard.size(),
               readFile(volume,"/bookmarks.bin.bookmark-edit.tmp").size());return 7;
 }
 if(!injected && publish && !archive)assert(primary==expected && backup==original && guard==original && readFile(volume,"/bookmarks.bin.bookmark-edit.tmp").empty());
 if(!injected && archive)assert(primary==expected && archivedBackup==original && archivedGuard==original && backup.empty() && guard.empty());
 if(!injected && !publish)assert(primary==original && readFile(volume,"/bookmarks.bin.bookmark-edit.tmp")==expected);
 if(injected && result==success) {
  std::fprintf(stderr,"success after injected fault: %s %s %d\n",argv[1],argv[2],fault);return 5;
 }
 if(injected && archive && !torn && primary==expected && backup.empty() && guard==original &&
    archivedBackup==original && archivedGuard.empty() &&
    Storage.lookupPath("/bookmarks.bin.bookmark-edit.bak")==HalStorage::PathStatus::Missing &&
    Storage.lookupPath("/bookmarks.bin.bookmark-edit.guard-0")==HalStorage::PathStatus::Missing) {
  assert(legacybookmarkedit::archivePublished("/bookmarks.bin",book,0,1)==Result::Archived);
  assert(readFile(volume,"/bookmarks.bin.bookmark-edit.guard-0")==original);
  std::printf("%s %s %d resumed archive\n",argv[1],argv[2],fault);
 }
 {auto later=volume.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);std::vector<uint8_t> bytes(32768,0x5a);
  assert(later && later.write(bytes.data(),bytes.size())==bytes.size() && later.sync() && later.close());}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(readFile(volume,"/bookmarks.bin")!=original && readFile(volume,"/bookmarks.bin.bookmark-edit.bak")!=original &&
    readFile(volume,"/bookmarks.bin.bookmark-edit.guard")!=original &&
    readFile(volume,"/bookmarks.bin.bookmark-edit.bak-0")!=original &&
    readFile(volume,"/bookmarks.bin.bookmark-edit.guard-0")!=original) {
  std::fprintf(stderr,"original lost after allocation: %s %s %d\n",argv[1],argv[2],fault);return 6;
 }
 assert(readFile(volume,"/progress.bin")==std::string("\x0c\x22\x38\x4e",4));
 if(!injected && archive)assert(legacybookmarkedit::stage("/bookmarks.bin",book,0,1)==Result::Ready);
 std::printf("%s %s %d %s\n",argv[1],argv[2],fault,injected?"injected":"not reached");
}
'''


def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        (tmp / 'test.cpp').write_text(code)
        (tmp / 'HalStorage.h').write_text('#pragma once\n')
        (tmp / 'Memory.h').write_text('#pragma once\n')
        (tmp / 'Logging.h').write_text('#pragma once\n')
        (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
        command = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
                   '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp), '-I', str(root / 'lib/hal'),
                   '-I', str(hal.dependency / 'src'), '-I', str(root / 'src/network/sync'), '-I', str(root / 'src'),
                   '-I', str(root / 'test/sync_staging/stubs'), '-I', str(root / 'src/activities/reader'),
                   '-I', str(root / 'lib/FsHelpers'), '-I', str(root / 'lib/Serialization'),
                   str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
        subprocess.run(command, check=True)
        for filesystem in ('fat', 'exfat'):
            for phase in ('stage', 'publish', 'stage-read', 'publish-read', 'stage-torn', 'publish-torn',
                          'archive', 'archive-read', 'archive-torn'):
                for fault in range(-1, 30):
                    subprocess.run([str(tmp / 'test'), filesystem, phase, str(fault)], check=True)
    print('Legacy bookmark publication and original preservation passed sampled FAT/exFAT faults')


if __name__ == '__main__':
    main()
