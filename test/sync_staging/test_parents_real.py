"""Exercise create-only parent folders on patched FAT/exFAT images."""
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
                    'bool mkdir(const char* p,bool recursive){return volume->mkdir(p,recursive);}\n bool exists(const char* p){return volume->exists(p);}\n bool openFileForWrite(const char*,const char* path,FsFile& file)')
code = code.replace('bool withStorageLock(bool (*)(void*),void*);',
                    'bool withStorageLock(bool (*)(void*),void*);bool mkdir(const char*,bool);')
code += hal.extract('bool HalStorage::mkdir(')
code += '\n#include "EnsureParents.h"\n' + (root/'src/network/sync/EnsureParents.cpp').read_text()
code += r'''
bool proceed(void*){return true;}
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/Authors",true));
 {auto f=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};assert(f && f.write(p,4)==4 && f.sync() && f.close());}
 if(scenario=="file"){auto f=volume.open("/Authors/Block",O_CREAT|O_EXCL|O_WRONLY);assert(f && f.close());}
 if(scenario=="unicode-existing" || scenario=="unicode-case")assert(volume.mkdir("/Authors/José",true));
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("write-"))disk.failAt=disk.writes+std::stoi(scenario.substr(6));
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 const char* path=scenario=="case"?"/AUTHORS/New/Book.epub":scenario=="file"?"/Authors/Block/Book.epub":
     scenario=="unicode-existing"?"/Authors/José/Book.epub":
     scenario=="unicode-case"?"/Authors/JOSÉ/Book.epub":
     scenario=="unicode-new"?"/Authors/José/Series/Book.epub":"/Authors/New/Series/Book.epub";
 auto& storage=HalStorage::getInstance();const auto result=syncpublish::ensureParents(storage,path,proceed,nullptr);
 if(disk.injected || disk.injectedRead)assert(result.state!=syncpublish::ParentState::Ready);
 else if(scenario=="case" || scenario=="file" || scenario=="unicode-case")assert(result.state==syncpublish::ParentState::Conflict);
 else if(scenario=="unicode-existing")assert(result.state==syncpublish::ParentState::Ready && result.created==0);
 else assert(result.state==syncpublish::ParentState::Ready && result.created==2);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injected || disk.injectedRead?"injected":"not reached");
 disk.failAt=disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 auto progress=volume.open("/progress.bin",O_RDONLY);uint8_t p[4]{};assert(progress && progress.read(p,4)==4 && progress.close());
 assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
 assert(!volume.exists(path));
 if(result.state==syncpublish::ParentState::Ready)assert(volume.exists(
     scenario=="unicode-existing"?"/Authors/José":scenario=="unicode-new"?"/Authors/José/Series":"/Authors/New/Series"));
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
        for scenario in ('normal','case','file','unicode-existing','unicode-new','unicode-case',*(f'write-{n}' for n in range(20)),*(f'read-{n}' for n in range(30))):
            subprocess.run([str(tmp/'test'),fs,scenario],check=True)
print('Parent creation passed sampled FAT/exFAT fixtures')
