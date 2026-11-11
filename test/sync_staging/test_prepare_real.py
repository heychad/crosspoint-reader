"""Verify intent preflight against patched FAT/exFAT and sampled I/O faults."""
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
implementation=(root/'src/network/sync/PreparePublication.cpp').read_text().replace('prepareLocked', 'preflightPrepareLocked')
code+='\n#include "DestinationGuard.h"\n'+(root/'src/network/sync/DestinationGuard.cpp').read_text()
code+='\n#include "CanonicalDestination.h"\n'+(root/'src/network/sync/CanonicalDestination.cpp').read_text()
code+='\n#include "StoredReferenceGuard.h"\n'+(root/'src/network/sync/StoredReferenceGuard.cpp').read_text()
code+='\n#include "PreparePublication.h"\n'+implementation
code+=r'''
struct TickContext {HalStorage* storage;int calls=0;bool race=false;};
bool tick(void* raw){auto& c=*static_cast<TickContext*>(raw);
 if(++c.calls==3 && c.race){auto f=c.storage->openChecked("/Authors/Book.epub",O_CREAT|O_EXCL|O_WRONLY);
  assert(f);uint8_t x=5;assert(f.write(&x,1)==1 && f.sync() && f.close());}return true;}
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/.crosspoint",true));assert(volume.mkdir("/Authors",true));
 {auto f=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(f && f.write(p,sizeof(p))==sizeof(p) && f.sync() && f.close());}
 auto& storage=HalStorage::getInstance();constexpr char name[]="__xsync_0123456789abcdef0123456789abcdef.part";
 constexpr char path[]="/__xsync_0123456789abcdef0123456789abcdef.part";
 std::vector<uint8_t> bytes(5000,42);StageWriter writer(storage);assert(writer.begin(name,bytes.size()));
 assert(writer.write(bytes.data(),bytes.size()) && writer.finish() && writer.takeResult());
 syncpublish::IntentView intent{name,"/Authors/Book.epub",bytes.size(),{}};CC_SHA256(bytes.data(),bytes.size(),intent.digest.data());
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("write-"))disk.failAt=disk.writes+std::stoi(scenario.substr(6));
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 TickContext ctx{&storage,0,scenario=="race"};
 const auto result=syncpublish::prepareVerifiedIntent(storage,intent,tick,&ctx);
 if(disk.injected || disk.injectedRead)assert(result!=syncpublish::PreflightResult::Prepared);
 else if(ctx.race)assert(result==syncpublish::PreflightResult::Changed);
 else assert(result==syncpublish::PreflightResult::Prepared);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injected || disk.injectedRead?"injected":"not reached");
 disk.failAt=disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 auto extra=volume.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);assert(extra);std::vector<uint8_t> allocation(32768,0x5a);
 assert(extra.write(allocation.data(),allocation.size())==allocation.size() && extra.sync() && extra.close());
 auto stage=volume.open(path,O_RDONLY);std::vector<uint8_t> actual(bytes.size());
 assert(stage && stage.read(actual.data(),actual.size())==static_cast<int>(actual.size()) && stage.close() && actual==bytes);
 auto progress=volume.open("/progress.bin",O_RDONLY);uint8_t p[4]{};
 assert(progress && progress.read(p,sizeof(p))==sizeof(p) && progress.close());assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
 if(result==syncpublish::PreflightResult::Prepared){auto f=volume.open(syncpublish::INTENT_PATH,O_RDONLY);syncpublish::Record record{};
  assert(f);auto size=f.fileSize();assert(size<=record.size() && f.read(record.data(),size)==static_cast<int>(size) && f.close());
  syncpublish::IntentView decoded{};assert(syncpublish::decode(record.data(),size,decoded));
  assert(decoded.stage==intent.stage && decoded.destination==intent.destination && decoded.digest==intent.digest);}
 if(ctx.race)assert(!volume.exists(syncpublish::INTENT_PATH));
}
'''
def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp=Path(directory);(tmp/'test.cpp').write_text(code)
        (tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n')
        (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
        hal=fixture.fixture.fixture.hal
        cmd=[os.environ.get('CXX','c++'),*hal.flags,*shlex.split(os.environ.get('CXXFLAGS','')),
             '-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),'-I',str(hal.dependency/'src'),
             '-I',str(root/'src/network/sync'),'-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src/activities/reader'),
             '-I',str(root/'src'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),'-I',str(root/'.pio/libdeps/x4pro/ArduinoJson/src'),str(tmp/'test.cpp'),
             *map(str,hal.files),'-o',str(tmp/'test')]
        subprocess.run(cmd,check=True)
        for fs in ('fat','exfat'):
            for scenario in ('normal','race',*(f'write-{n}' for n in range(12)),*(f'read-{n}' for n in range(20))):
                subprocess.run([str(tmp/'test'),fs,scenario],check=True)
    print('Verified intent preflight passed FAT/exFAT fixtures')

if __name__ == "__main__":
    main()
