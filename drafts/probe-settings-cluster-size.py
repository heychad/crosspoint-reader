"""Check settings snapshots against small and X4-sized FAT clusters."""
from pathlib import Path

base = Path(__file__).with_name('probe-settings-real-fs.py')
code = base.read_text()
code = code.replace("'#include <SettingsRecoveryProbe.h>'", "'#include <SettingsRecoveryProbe.h>\\n#include <SettingsSnapshotCopy.h>'")
start = code.index("body=r'''\n")
end = code.index("\n'''\nwith tempfile.TemporaryDirectory()", start) + 4
body = r'''body=r\x27\x27\x27
int main() {
 for(bool large:{false,true}) {
  Disk disk;disk.sectorsTotal=large?64*1024*1024:65536;
  uint8_t scratch[512];FatFormatter fmt;assert(fmt.format(&disk,scratch));
  FsVolume volume;assert(volume.begin(&disk));Storage.attach(volume);
  const std::string payload="{\"data\":\""+std::string(2268,'x')+"\"}";
  auto file=Storage.open("/settings.json",O_CREAT|O_EXCL|O_WRONLY);assert(file);
  assert(file.write(payload.data(),payload.size())==payload.size()&&file.close());
  auto result=settingsrecovery::copySnapshot("/settings.json","/snapshot.json");
  const size_t cluster=volume.bytesPerCluster();
  assert(large && cluster==32768 ? result==settingsrecovery::CopyResult::Copied
                                : cluster<payload.size() && result==settingsrecovery::CopyResult::Failed);
  if(!large)assert(!volume.exists("/snapshot.json"));
  FsVolume after;assert(after.begin(&disk));Storage.attach(after);
  auto original=Storage.open("/settings.json",O_RDONLY);assert(original);
  std::string read(payload.size(),'\0');assert(original.read(read.data(),read.size())==(int)read.size());
  assert(read==payload && original.close());
  std::printf("FAT%u cluster=%zu settings-bytes=%zu snapshot=%s source-preserved\n",
    unsigned(volume.fatType()),cluster,payload.size(),result==settingsrecovery::CopyResult::Copied?"verified":"refused");
 }
 {
  Disk disk;disk.sectorsTotal=65536;uint8_t scratch[512];FatFormatter fmt;assert(fmt.format(&disk,scratch));
  Cluster_t first=0;Sector_t fatStart=0;uint8_t fatCount=0;size_t cluster=0;
  const std::string payload="{\"data\":\""+std::string(2039,'a')+std::string(229,'z')+"\"}";
  {FsVolume volume;assert(volume.begin(&disk));Storage.attach(volume);assert(volume.fatType()==16);
   cluster=volume.bytesPerCluster();assert(cluster==2048);fatStart=volume.fatStartSector();fatCount=volume.fatCount();
   auto file=volume.open("/settings.json",O_CREAT|O_EXCL|O_WRONLY);assert(file);
   assert(file.write(payload.data(),payload.size())==payload.size());first=file.firstCluster();assert(file.close());}
  auto le16=[](const uint8_t* p){return uint16_t(p[0]|uint16_t(p[1])<<8);};
  auto le32=[&](const uint8_t* p){return uint32_t(le16(p)|uint32_t(le16(p+2))<<16);};
  uint8_t mbr[512],boot[512];assert(disk.readSector(0,mbr)&&disk.readSector(le32(mbr+454),boot));
  const uint32_t fatSize=le16(boot+22)?le16(boot+22):le32(boot+36);assert(fatSize);
  const uint64_t firstOffset=uint64_t(first)*2;const auto& firstSector=disk.sectors[fatStart+firstOffset/512];
  const Cluster_t second=le16(firstSector.data()+firstOffset%512);assert(second>=2);
  const uint64_t offset=uint64_t(second)*2;
  for(unsigned i=0;i<fatCount;++i){auto& sector=disk.sectors[fatStart+Sector_t(i)*fatSize+offset/512];
   sector[offset%512]=0;sector[offset%512+1]=0;}
  FsVolume volume;assert(volume.begin(&disk));Storage.attach(volume);
  auto result=settingsrecovery::copySnapshot("/settings.json","/snapshot.json");
  assert(result==settingsrecovery::CopyResult::Failed&&!volume.exists("/snapshot.json"));
  FsVolume after;assert(after.begin(&disk));Storage.attach(after);
  auto original=Storage.open("/settings.json",O_RDONLY);assert(original);
  std::string read(payload.size(),'\0');assert(original.read(read.data(),read.size())==(int)read.size());
  assert(read==payload && original.close());
  std::puts("FAT16 free-marked source tail refused before allocation; source preserved");
 }
 {
  Disk disk;uint8_t scratch[512];ExFatFormatter fmt;assert(fmt.format(&disk,scratch));
  Cluster_t cluster=0;Sector_t bitmapStart=0;
  const std::string payload="{\"data\":\""+std::string(2268,'x')+"\"}";
  {FsVolume volume;assert(volume.begin(&disk));bitmapStart=volume.dataStartSector();
   auto file=volume.open("/settings.json",O_CREAT|O_EXCL|O_WRONLY);assert(file);
   assert(file.write(payload.data(),payload.size())==payload.size());cluster=file.firstCluster();assert(file.close());}
  assert(cluster>=2);const uint32_t bit=cluster-2;const Sector_t sector=bitmapStart+bit/(512*8);
  const size_t byte=(bit/8)%512;const uint8_t mask=uint8_t{1}<<(bit%8);
  assert(disk.sectors[sector][byte]&mask);disk.sectors[sector][byte]&=static_cast<uint8_t>(~mask);
  FsVolume volume;assert(volume.begin(&disk));Storage.attach(volume);
  auto result=settingsrecovery::copySnapshot("/settings.json","/snapshot.json");
  assert(result==settingsrecovery::CopyResult::Failed&&!volume.exists("/snapshot.json"));
  FsVolume after;assert(after.begin(&disk));Storage.attach(after);
  auto original=Storage.open("/settings.json",O_RDONLY);assert(original);
  std::string read(payload.size(),'\0');assert(original.read(read.data(),read.size())==(int)read.size());
  assert(read==payload && original.close());
  std::puts("exFAT free-marked source bitmap refused before allocation; source preserved");
 }
}
\x27\x27\x27'''.replace('\\x27', "'").replace('\\\\', '\\')
code = code[:start] + body + code[end:]
code = code.replace("'  size_t fileSize() const { return file_->fileSize(); }'",
                    "'  size_t fileSize() const { return file_->fileSize(); }'")
code = code.replace("'\\n  uint64_t fileSize64() const { return file_->fileSize(); }",
                    "'\\n  size_t write(const void* p, size_t n) { return file_->write(p, n); }\\n  uint64_t fileSize64() const { return file_->fileSize(); }")
code = code.replace("str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),*map",
                    "str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),str(root/'lib/Serialization/SettingsSnapshotCopy.cpp'),*map")
exec(compile(code, str(base), 'exec'), {'__file__': str(base), '__name__': '__main__'})
