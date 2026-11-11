"""Exercise read-only recovery preview against patched FAT/exFAT images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('intent_real',root/'test/sync_staging/test_intent_real.py')
fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
code=fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code += '\n#include "RecoveryPreview.h"\n' + (root/'src/network/sync/RecoveryPreview.cpp').read_text()
code += r'''
int main(int argc,char** argv){
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;assert(volume.mkdir("/.crosspoint",true));assert(volume.mkdir("/Authors",true));
 {auto f=volume.open("/progress.bin",O_CREAT|O_EXCL|O_WRONLY);const uint8_t p[]={12,34,56,78};
  assert(f && f.write(p,sizeof(p))==sizeof(p) && f.sync() && f.close());}
 auto& storage=HalStorage::getInstance();constexpr char name[]="__xsync_0123456789abcdef0123456789abcdef.part";
 constexpr char target[]="/Authors/Book.epub";std::vector<uint8_t> bytes(5000,42);
 syncpublish::IntentView intent{name,target,bytes.size(),{}};CC_SHA256(bytes.data(),bytes.size(),intent.digest.data());
 if(scenario!="missing"){
  StageWriter writer(storage);assert(writer.begin(name,bytes.size()));
  if(scenario=="mismatch")bytes[0]^=1;
  assert(writer.write(bytes.data(),bytes.size()) && writer.finish() && writer.takeResult());
 }
 if(scenario=="occupied" || scenario=="occupied-match" || scenario.starts_with("targetread-")){
  auto f=storage.openChecked(target,O_CREAT|O_EXCL|O_WRONLY);assert(f);
  if(scenario=="occupied"){uint8_t x=3;assert(f.write(&x,1)==1);}
  else assert(f.write(bytes.data(),bytes.size())==bytes.size());
  assert(f.sync() && f.close());
 }
 if(scenario=="copy-match" || scenario=="copy-mismatch"){
  if(scenario=="copy-mismatch")bytes[0]^=1;
  auto f=storage.openChecked("/.crosspoint/__xsync_0123456789abcdef0123456789abcdef.part",O_CREAT|O_EXCL|O_WRONLY);
  assert(f && f.write(bytes.data(),bytes.size())==bytes.size() && f.sync() && f.close());
 }
 assert(syncpublish::prepareIntent(storage,intent)==syncpublish::PrepareResult::Prepared);
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 if(scenario.starts_with("targetread-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(11));
 const int before=disk.writes;auto output=std::make_unique<syncpublish::RecoveryPreview>();output->size=77;
 auto tick=[](void*){return true;};auto result=syncpublish::previewRecovery(storage,*output,tick,nullptr);
 if(disk.injectedRead){assert(result!=syncpublish::PreviewResult::Observed && output->size==77);}
 else {
  assert(result==syncpublish::PreviewResult::Observed && output->size==5000);
  if(scenario=="missing")assert(output->stageState==syncpublish::StageState::Missing);
  else if(scenario=="mismatch")assert(output->stageState==syncpublish::StageState::Mismatch);
  else assert(output->stageState==syncpublish::StageState::Matches);
  assert(output->copyState==(scenario=="copy-match"?syncpublish::StageState::Matches:
                             scenario=="copy-mismatch"?syncpublish::StageState::Mismatch:syncpublish::StageState::Missing));
  assert(output->destinationState==(scenario=="occupied"?syncpublish::DestinationState::Mismatch:
   (scenario=="occupied-match" || scenario.starts_with("targetread-"))?syncpublish::DestinationState::Matches:syncpublish::DestinationState::Missing));
 }
 assert(disk.writes==before);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injectedRead?"injected":"not reached");
 disk.failReadAt=-1;SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));
 auto progress=volume.open("/progress.bin",O_RDONLY);uint8_t p[4]{};
 assert(progress && progress.read(p,sizeof(p))==sizeof(p) && progress.close());
 assert(p[0]==12 && p[1]==34 && p[2]==56 && p[3]==78);
}
'''
def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp=Path(directory);(tmp/'test.cpp').write_text(code)
        (tmp/'HalStorage.h').write_text('#pragma once\n');(tmp/'Memory.h').write_text('#pragma once\n')
        (tmp/'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
        cmd=[os.environ.get('CXX','c++'),*fixture.fixture.hal.flags,*shlex.split(os.environ.get('CXXFLAGS','')),
             '-include',str(tmp/'HostCompat.h'),'-I',str(tmp),'-I',str(root/'lib/hal'),
             '-I',str(fixture.fixture.hal.dependency/'src'),'-I',str(root/'src/network/sync'),
             '-I',str(root/'src'),'-I',str(root/'test/sync_staging/stubs'),'-I',str(root/'src/activities/reader'),
             '-I',str(root/'src'),'-I',str(root/'lib/FsHelpers'),'-I',str(root/'lib/Serialization'),
             str(tmp/'test.cpp'),*map(str,fixture.fixture.hal.files),'-o',str(tmp/'test')]
        subprocess.run(cmd,check=True)
        for fs in ('fat','exfat'):
            for scenario in ('missing','match','mismatch','occupied','occupied-match','copy-match','copy-mismatch',
                             *(f'read-{n}' for n in range(20)),*(f'targetread-{n}' for n in range(30))):
                subprocess.run([str(tmp/'test'),fs,scenario],check=True)
    print('Read-only recovery preview passed FAT/exFAT filesystem fixtures')

if __name__ == "__main__":
    main()
