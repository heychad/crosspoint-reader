"""Exercise destination-sidecar checks on patched FAT and exFAT images."""
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
code+='\n#include "DestinationGuard.h"\n'+(root/'src/network/sync/DestinationGuard.cpp').read_text()
code+=r'''
struct GuardTick {HalStorage* storage;int calls=0;bool race=false;};
bool guardTick(void* raw){auto& c=*static_cast<GuardTick*>(raw);
 if(++c.calls==3 && c.race){auto f=c.storage->openChecked("/Authors/race.epub",O_CREAT|O_EXCL|O_WRONLY);
  assert(f);uint8_t x=5;assert(f.write(&x,1)==1 && f.sync() && f.close());}return true;}
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
 assert(volume.mkdir("/.crosspoint/bookmarks",true) && volume.mkdir("/.crosspoint/reading_stats",true) && volume.mkdir("/Authors",true));
 {auto f=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(f && f.write(p,sizeof(p))==sizeof(p) && f.sync() && f.close());}
 const std::string dest="/Authors/Book.epub";const auto native=std::hash<std::string>{}(dest);
 std::string conflict;
 if(scenario=="sidecar")conflict=dest+".meta.json";
 if(scenario=="cache")conflict="/.crosspoint/reading_stats/epub_"+std::to_string(native);
 if(scenario=="bookmark")conflict="/.crosspoint/bookmarks/Authors_Book.json.bak.move-tmp";
 if(!conflict.empty()){auto f=volume.open(conflict.c_str(),O_CREAT|O_EXCL|O_WRONLY);uint8_t x=9;
  assert(f && f.write(&x,1)==1 && f.sync() && f.close());}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 auto& storage=HalStorage::getInstance();GuardTick ctx{&storage,0,scenario=="race"};const int writes=disk.writes;
 const auto result=syncpublish::inspectDestinationState(storage,dest,guardTick,&ctx);
 if(disk.injectedRead)assert(result!=syncpublish::DestinationGuardResult::Clear);
 else if(ctx.race)assert(result==syncpublish::DestinationGuardResult::Changed);
 else if(!conflict.empty())assert(result==syncpublish::DestinationGuardResult::Conflict);
 else assert(result==syncpublish::DestinationGuardResult::Clear);
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
         '-I',str(root/'src/network/sync'),'-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src/activities/reader'),
         '-I',str(root/'src'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),str(tmp/'test.cpp'),
         *map(str,hal.files),'-o',str(tmp/'test')]
    subprocess.run(cmd,check=True)
    for fs in ('fat','exfat'):
        for scenario in ('clear','sidecar','cache','bookmark','race',*(f'read-{n}' for n in range(25))):
            subprocess.run([str(tmp/'test'),fs,scenario],check=True)
print('Destination-derived state checks passed FAT/exFAT fixtures')
