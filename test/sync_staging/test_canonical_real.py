"""Exercise canonical destination checks with patched FAT/exFAT images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('preview_real',root/'test/sync_staging/test_preview_real.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
code=fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code+='\n#include "CanonicalDestination.h"\n'+(root/'src/network/sync/CanonicalDestination.cpp').read_text()
code+=r'''
struct CanonicalTick {HalStorage* storage;int calls=0;bool race=false;};
bool canonicalTick(void* raw){auto& c=*static_cast<CanonicalTick*>(raw);
 if(++c.calls==3 && c.race){auto f=c.storage->openChecked("/Authors/race.epub",O_CREAT|O_EXCL|O_WRONLY);
  assert(f);uint8_t x=5;assert(f.write(&x,1)==1 && f.sync() && f.close());}return true;}
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
 assert(volume.mkdir("/.crosspoint",true) && volume.mkdir("/Authors/Writer",true));
 {auto f=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(f && f.write(p,sizeof(p))==sizeof(p) && f.sync() && f.close());}
 {auto f=volume.open("/__xsync_0123456789abcdef0123456789abcdef.part",O_CREAT|O_EXCL|O_WRONLY);uint8_t x=9;
  assert(f && f.write(&x,1)==1 && f.sync() && f.close());}
 if(scenario=="temporary"){auto f=volume.open("/Authors/Writer/other.epub.part",O_CREAT|O_EXCL|O_WRONLY);uint8_t x=9;
  assert(f && f.write(&x,1)==1 && f.sync() && f.close());}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 auto& storage=HalStorage::getInstance();CanonicalTick ctx{&storage,0,scenario=="race"};const int writes=disk.writes;
 const char* path=scenario=="case"?"/authors/Writer/Book.epub":scenario=="missing"?"/Authors/Missing/Book.epub":
                  scenario=="root"?"/Book.epub":"/Authors/Writer/Book.epub";
 const auto result=syncpublish::inspectCanonicalDestination(storage,path,canonicalTick,&ctx);
 if(disk.injectedRead)assert(result!=syncpublish::CanonicalResult::Clear);
 else if(ctx.race)assert(result==syncpublish::CanonicalResult::Changed);
 else if(scenario=="temporary")assert(result==syncpublish::CanonicalResult::Conflict);
 else if(scenario=="case")assert(result==syncpublish::CanonicalResult::NonCanonical);
 else if(scenario=="missing")assert(result==syncpublish::CanonicalResult::MissingParent);
 else assert(result==syncpublish::CanonicalResult::Clear);
 if(!ctx.race)assert(disk.writes==writes);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injectedRead?"injected":"not reached");
 disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 auto progress=volume.open("/progress.bin",O_RDONLY);uint8_t p[4]{};
 assert(progress && progress.read(p,sizeof(p))==sizeof(p) && progress.close());
 assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp=Path(directory);(tmp/'test.cpp').write_text(code)
    (tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n')
    (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    hal=fixture.fixture.fixture.hal
    cmd=[os.environ.get('CXX','c++'),*hal.flags,*shlex.split(os.environ.get('CXXFLAGS','')),
         '-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),'-I',str(hal.dependency/'src'),
         '-I',str(root/'src/network/sync'),'-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),
         '-I',str(root/'src/activities/reader'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),
         str(tmp/'test.cpp'),*map(str,hal.files),'-o',str(tmp/'test')]
    subprocess.run(cmd,check=True)
    for fs in ('fat','exfat'):
        for scenario in ('clear','temporary','case','missing','root','race',*(f'read-{n}' for n in range(30))):
            subprocess.run([str(tmp/'test'),fs,scenario],check=True)
print('Canonical destination scan passed FAT/exFAT fixtures')
