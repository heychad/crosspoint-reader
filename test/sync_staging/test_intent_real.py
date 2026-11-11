"""Exercise the publication-intent store with real SdFat images and HAL methods."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('stage_real',root/'test/sync_staging/test_real.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
code=fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code += '\n#include "PublishIntentStore.h"\n' + (root/'src/network/sync/PublishIntentStore.cpp').read_text()
code += r'''
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/.crosspoint",true));assert(volume.mkdir("/books",true));
 {auto f=volume.open("/books/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(f && f.write(p,sizeof(p))==sizeof(p) && f.sync() && f.close());}
 auto& storage=HalStorage::getInstance();
 syncpublish::IntentView intent{"__xsync_0123456789abcdef0123456789abcdef.part","/Authors/Book.epub",5000,{}};
 if(scenario.starts_with("write-"))disk.failAt=disk.writes+std::stoi(scenario.substr(6));
 if(scenario.starts_with("read-")){SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
  disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));}
 const auto result=syncpublish::prepareIntent(storage,intent);
 if(disk.injected || disk.injectedRead)assert(result!=syncpublish::PrepareResult::Prepared);
 else assert(result==syncpublish::PrepareResult::Prepared);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injected || disk.injectedRead?"injected":"not reached");
 disk.failAt=disk.failReadAt=-1;
 if(result==syncpublish::PrepareResult::Prepared){
  syncpublish::Record output{};size_t size=0;assert(syncpublish::inspectIntent(storage,output,size)==syncpublish::InspectResult::Valid);
  syncpublish::IntentView decoded{};assert(syncpublish::decode(output.data(),size,decoded));
  assert(decoded.stage==intent.stage && decoded.destination==intent.destination && decoded.size==intent.size);
  assert(syncpublish::prepareIntent(storage,intent)==syncpublish::PrepareResult::Existing);
 }
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 auto extra=volume.open("/books/later.bin",O_CREAT|O_EXCL|O_WRONLY);assert(extra);std::vector<uint8_t> bytes(32768,0x5a);
 assert(extra.write(bytes.data(),bytes.size())==bytes.size() && extra.sync() && extra.close());
 if(result==syncpublish::PrepareResult::Prepared){
  auto f=volume.open(syncpublish::INTENT_PATH,O_RDONLY);syncpublish::Record record{};assert(f);
  const auto length=f.fileSize();assert(length<=record.size());
  assert(f.read(record.data(),length)==static_cast<int>(length) && f.close());
  syncpublish::IntentView decoded{};assert(syncpublish::decode(record.data(),length,decoded));
  assert(decoded.stage==intent.stage && decoded.destination==intent.destination && decoded.size==intent.size);
 }
 auto progress=volume.open("/books/progress.bin",O_RDONLY);uint8_t p[4]{};
 assert(progress && progress.read(p,sizeof(p))==sizeof(p) && progress.close());
 assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
}
'''
def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp=Path(directory);(tmp/'test.cpp').write_text(code)
        (tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n')
        (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
        cmd=[os.environ.get('CXX','c++'),*fixture.hal.flags,*shlex.split(os.environ.get('CXXFLAGS','')),
             '-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),
             '-I',str(fixture.hal.dependency/'src'),'-I',str(root/'src/network/sync'),
             '-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src/activities/reader'),
             '-I',str(root/'src'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),
             str(tmp/'test.cpp'),*map(str,fixture.hal.files),'-o',str(tmp/'test')]
        subprocess.run(cmd,check=True)
        for fs in ('fat','exfat'):
            for scenario in ('normal',*(f'write-{n}' for n in range(20)),*(f'read-{n}' for n in range(20))):
                subprocess.run([str(tmp/'test'),fs,scenario],check=True)
    print('Publication intent store passed FAT/exFAT fault fixtures')

if __name__ == "__main__":
    main()
