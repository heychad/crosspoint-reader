"""Run section-cache rename recovery on patched FAT/exFAT filesystem images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('preview_real',root/'test/sync_staging/test_preview_real.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
hal=fixture.fixture.fixture.hal
code=fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code=code.replace('bool openFileForWrite(const char*,const char* path,FsFile& file)',
                  'bool rename(const char* a,const char* b){return volume->rename(a,b);}\n bool remove(const char* p){return volume->remove(p);}\n bool exists(const char* p){return volume->exists(p);}\n bool openFileForWrite(const char*,const char* path,FsFile& file)')
code=code.replace('bool withStorageLock(bool (*)(void*),void*);',
                  'bool withStorageLock(bool (*)(void*),void*);bool rename(const char*,const char*);bool remove(const char*);bool distinctMoveChains(const char*,const char*);bool openFileForRead(const char*,const std::string&,HalFile&);')
code=code.replace('uint64_t fileSize64();int read(void*,size_t);', 'uint64_t fileSize64();size_t fileSize();int read(void*,size_t);')
code+='\n#define HAL_STORAGE_MUTATION_CALL(method, ...) HalStorage::StorageLock lock; mutationFence.mutationAttempt(); const bool ok=SDCard.method(__VA_ARGS__); if(!ok)mutationFence.poison(); return ok;\n'
code+=hal.extract('bool HalStorage::rename(')+'\n'+hal.extract('bool HalStorage::remove(')
code+='\n#define LOG_ERR(...) ((void)0)\n#include "SmallFileIdentity.h"\n'+hal.extract('bool HalStorage::distinctMoveChains(')
code+='\nsize_t HalFile::fileSize(){return impl->file.fileSize();}\n'
code+='bool HalStorage::openFileForRead(const char*,const std::string& p,HalFile& f){f=openChecked(p.c_str(),O_RDONLY);return bool(f);}\n'
source=(root/'lib/Epub/Epub/Section.cpp').read_text()
def extract(name,return_type='bool'):
    start=source.index(return_type+' '+name+'(');opening=source.index('{',start);depth=0
    for index in range(opening,len(source)):
        depth+=(source[index]=='{')-(source[index]=='}')
        if not depth:return source[start:index+1]
    raise RuntimeError(name)
code+='\n#define Storage HalStorage::getInstance()\nconstexpr size_t HEADER_SIZE=3;\n'+extract('recoverSectionCache')+'\n'+extract('promoteSectionCache')+'\n'+extract('retireSectionCacheBackup','void')
code+=r'''
std::string readFile(FsVolume& volume,const char* path){auto f=volume.open(path,O_RDONLY);if(!f)return {};char data[8]{};int n=f.read(data,sizeof(data));if(n<0 || !f.close())return {};return std::string(data,n);}
int main(int argc,char** argv){
 assert(argc==3 || argc==4);bool exfat=std::string(argv[1])=="exfat";int fault=std::stoi(argv[2]);
 const bool recoveryFault=argc==4 && std::string(argv[3])=="recovery";Disk disk;uint8_t scratch[512];
 if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/cache",true));
 const char* old="/cache/section.bin";const char* tmp="/cache/section.bin.part";const char* bak="/cache/section.bin.bak";
 if(recoveryFault){
  {auto f=volume.open(old,O_CREAT|O_EXCL|O_WRONLY);assert(f && f.sync() && f.close());}
  {auto f=volume.open(bak,O_CREAT|O_EXCL|O_WRONLY);assert(f && f.write("old",3)==3 && f.sync() && f.close());}
  SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
  if(fault>=0){disk.failStop=true;disk.failAt=disk.writes+fault;}
  const bool recovered=recoverSectionCache(old);const bool injected=disk.injected;
  disk.dead=false;disk.failAt=-1;monitorFault=false;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
  auto current=readFile(volume,old),backup=readFile(volume,bak),damaged=readFile(volume,"/cache/section.bin.bad-0");
  if(current!="old" && backup!="old" && damaged!="old"){
   std::fprintf(stderr,"lost recovery cache: fs=%s fault=%d injected=%d recovered=%d current=%s backup=%s damaged=%s\n",argv[1],fault,injected,recovered,current.c_str(),backup.c_str(),damaged.c_str());return 4;
  }
  if(!injected)assert(recovered && current=="old");
  bool retry=false;
  if(injected && current!="old"){
   retry=recoverSectionCache(old);
   SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
   current=readFile(volume,old);backup=readFile(volume,bak);damaged=readFile(volume,"/cache/section.bin.bad-0");
   if(current!="old" && backup!="old" && damaged!="old")return 5;
   if(!retry && backup=="old")std::printf("%s recovery=%d unresolved lookup old=%d backup=%d bad0=%d bad1=%d\n",argv[1],fault,
    static_cast<int>(Storage.lookupPath(old)),static_cast<int>(Storage.lookupPath(bak)),
    static_cast<int>(Storage.lookupPath("/cache/section.bin.bad-0")),static_cast<int>(Storage.lookupPath("/cache/section.bin.bad-1")));
  }
  if(current=="old" && backup=="old"){
   const bool independent=Storage.distinctMoveChains(old,bak);
   retireSectionCacheBackup(old);
   SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
   assert(readFile(volume,old)=="old");
   if(!independent){assert(readFile(volume,bak)=="old");std::printf("%s recovery=%d overlapping cache names retained\n",argv[1],fault);}
  }
  std::printf("%s recovery=%d %s retry=%d current=%s backup=%s damaged=%s\n",argv[1],fault,injected?"injected":"not reached",retry,current.c_str(),backup.c_str(),damaged.c_str());return 0;
 }
 for(auto p:{old,tmp}){auto f=volume.open(p,O_CREAT|O_EXCL|O_WRONLY);const char* text=p==old?"old":"new";assert(f && f.write(text,3)==3 && f.sync() && f.close());}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(fault>=0){disk.failStop=true;disk.failAt=disk.writes+fault;}
 const bool published=promoteSectionCache(tmp,old);
 bool injected=disk.injected;disk.dead=false;disk.failAt=-1;monitorFault=false;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 const std::string current=readFile(volume,old), backup=readFile(volume,bak), stage=readFile(volume,tmp);
 if(published){assert(current=="new");}
 else if(current!="old" && backup!="old"){
   std::fprintf(stderr,"lost old cache: fs=%s fault=%d injected=%d current=%s backup=%s stage=%s\n",argv[1],fault,injected,current.c_str(),backup.c_str(),stage.c_str());return 2;
 }
 if(!published && current.empty() && backup=="old"){
   const int beforeLookup=static_cast<int>(Storage.lookupPath(old));
   assert(recoverSectionCache(old));
   const int afterLookup=static_cast<int>(Storage.lookupPath(old));
   const auto beforeRemount=readFile(volume,old);
   auto detail=volume.open(old,O_RDONLY);const bool opened=bool(detail);const auto detailSize=opened?detail.fileSize():0; if(opened)detail.close();
   SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
   const auto restored=readFile(volume,old), remaining=readFile(volume,bak);
   if(restored!="old" || !remaining.empty()){
     std::fprintf(stderr,"restore remount mismatch: fs=%s fault=%d lookup=%d/%d opened=%d size=%llu pre=%s restored=%s backup=%s\n",argv[1],fault,beforeLookup,afterLookup,opened,static_cast<unsigned long long>(detailSize),beforeRemount.c_str(),restored.c_str(),remaining.c_str());return 3;
   }
 }
 if(!injected)assert(published && current=="new");
 std::printf("%s fault=%d %s current=%s backup=%s\n",argv[1],fault,injected?"injected":"not reached",current.c_str(),backup.c_str());
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'test.cpp').write_text(code);(tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n');(tmp/'Logging.h').write_text('#pragma once\n')
    (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    cmd=[os.environ.get('CXX','c++'),*hal.flags,*shlex.split(os.environ.get('CXXFLAGS','')),'-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),'-I',str(hal.dependency/'src'),'-I',str(root/'src/network/sync'),'-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src/activities/reader'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),str(tmp/'test.cpp'),*map(str,hal.files),'-o',str(tmp/'test')]
    subprocess.run(cmd,check=True)
    for fs in ('fat','exfat'):
        for n in range(-1,16):subprocess.run([str(tmp/'test'),fs,str(n)],check=True)
        for n in range(-1,16):subprocess.run([str(tmp/'test'),fs,str(n),'recovery'],check=True)
print('Section cache promotion passed FAT/exFAT fail-stop fixtures')
