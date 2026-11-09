"""Fault-test the isolated settings snapshot against in-memory FAT and exFAT."""
from pathlib import Path
base=Path(__file__).with_name('probe-settings-real-fs.py')
code=base.read_text()
code=code.replace("'#include <SettingsRecoveryProbe.h>'", "'#include <SettingsRecoveryProbe.h>\\n#include <SettingsSnapshotCopy.h>'")
start=code.index("body=r'''\n")
end=code.index("\n'''\nwith tempfile.TemporaryDirectory()",start)+4
body=r'''body=r\x27\x27\x27
int main(){
 for(bool exfat:{false,true}){
  Disk baseline;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&baseline,scratch));}else{FatFormatter f;assert(f.format(&baseline,scratch));}
  const std::string payload="{\\"data\\":\\""+std::string(2268,'x')+"\\"}";
  {FsVolume v;assert(v.begin(&baseline));Storage.attach(v);auto f=Storage.open("/settings.json",O_CREAT|O_EXCL|O_WRONLY);assert(f);assert(f.write(reinterpret_cast<const uint8_t*>(payload.data()),payload.size())==payload.size());assert(f.close());}
  for(bool stop:{false,true}){int injections=0;for(int fault=-1;fault<128;++fault){
   Disk disk;disk.sectors=baseline.sectors;settingsrecovery::CopyResult result;
   {FsVolume v;assert(v.begin(&disk));Storage.attach(v);disk.writes=0;disk.failAt=fault;disk.failStop=stop;result=settingsrecovery::copySnapshot("/settings.json","/settings.snapshot");}
   if(fault>=0 && !disk.injected)break;if(disk.injected)++injections;
   disk.dead=false;disk.failAt=-1;FsVolume remount;assert(remount.begin(&disk));Storage.attach(remount);
   auto f=Storage.open("/settings.json",O_RDONLY);if(!f){std::fprintf(stderr,"source lost: %s stop=%d fault=%d\\n",exfat?"exfat":"fat",stop,fault);return 2;}
   std::string read(payload.size(),'\\0');if(f.fileSize()!=payload.size()||f.read(read.data(),read.size())!=(int)read.size()||read!=payload||!f.close()){std::fprintf(stderr,"source changed: %s stop=%d fault=%d\\n",exfat?"exfat":"fat",stop,fault);return 3;}
   if(result==settingsrecovery::CopyResult::Copied){auto copy=Storage.open("/settings.snapshot",O_RDONLY);assert(copy);std::string readCopy(payload.size(),'\\0');assert(copy.fileSize()==payload.size()&&copy.read(readCopy.data(),readCopy.size())==(int)readCopy.size()&&readCopy==payload&&copy.close());}
   const std::string laterBytes(8192,'z');auto later=Storage.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);assert(later);assert(later.write(reinterpret_cast<const uint8_t*>(laterBytes.data()),laterBytes.size())==laterBytes.size());assert(later.close());
   FsVolume after;assert(after.begin(&disk));Storage.attach(after);
   auto preserved=Storage.open("/settings.json",O_RDONLY);if(!preserved){std::fprintf(stderr,"source lost after allocation: %s stop=%d fault=%d\\n",exfat?"exfat":"fat",stop,fault);return 4;}
   std::string finalRead(payload.size(),'\\0');if(preserved.fileSize()!=payload.size()||preserved.read(finalRead.data(),finalRead.size())!=(int)finalRead.size()||finalRead!=payload||!preserved.close()){std::fprintf(stderr,"source changed after allocation: %s stop=%d fault=%d\\n",exfat?"exfat":"fat",stop,fault);return 5;}
   if(result==settingsrecovery::CopyResult::Copied){auto copy=Storage.open("/settings.snapshot",O_RDONLY);assert(copy);std::string finalCopy(payload.size(),'\\0');if(copy.fileSize()!=payload.size()||copy.read(finalCopy.data(),finalCopy.size())!=(int)finalCopy.size()||finalCopy!=payload||!copy.close()){std::fprintf(stderr,"snapshot changed after allocation: %s stop=%d fault=%d\\n",exfat?"exfat":"fat",stop,fault);return 6;}}
  }std::printf("%s %s injected write failures=%d source preserved\\n",exfat?"exFAT":"FAT",stop?"fail-stop":"transient",injections);assert(injections>0);}
 }
}
\x27\x27\x27'''.replace('\\x27',"'").replace('\\\\', '\\')
code=code[:start]+body+code[end:]
code=code.replace("'#include <SettingsRecoveryProbe.h>\\n#include <SettingsSnapshotCopy.h>'", "'#include <SettingsRecoveryProbe.h>\\n#include <SettingsSnapshotCopy.h>'")
code=code.replace("'  size_t fileSize() const { return file_->fileSize(); }'", "'  size_t fileSize() const { return file_->fileSize(); }'")
code=code.replace("'\\n  uint64_t fileSize64() const { return file_->fileSize(); }", "'\\n  size_t write(const void* p, size_t n) { return file_->write(p, n); }\\n  uint64_t fileSize64() const { return file_->fileSize(); }")
code=code.replace("str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),*map", "str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),str(root/'lib/Serialization/SettingsSnapshotCopy.cpp'),*map")
exec(compile(code,str(base),'exec'),{'__file__':str(base),'__name__':'__main__'})
