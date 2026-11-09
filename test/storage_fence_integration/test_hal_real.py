"""Exercise production HAL snapshot hooks with real SdFat disk images."""
import ast
from pathlib import Path
import hashlib
import importlib.util
import os
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/hal/HalStorage.cpp').read_text()
dependency = ROOT / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('patch_sdfat', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
for _, relative, _, expected in patcher.PATCHES:
    assert hashlib.sha256((dependency / relative).read_bytes()).hexdigest() == expected

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for i in range(opening, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if not depth:
                return source[start:i + 1]
    raise AssertionError(signature)

impl = source[source.index('class HalFile::Impl {'):source.index('\nHalFile::HalFile() = default;')]
constructors = source[source.index('HalFile::HalFile() = default;'):source.index('\nHalStorage::SnapshotRead HalStorage::readDirectoryRecord(')]
signatures = [
    'bool HalStorage::begin()', 'bool HalStorage::beginMutationSnapshot(', 'bool HalStorage::mutationSnapshotUnchanged(',
    'bool HalStorage::monitoredStorageHealthy()', 'bool HalStorage::withStorageLock(',
    'bool HalStorage::captureMutationMarker(', 'bool HalStorage::mutationMarkerUnchanged(',
    'HalStorage::SnapshotFormat HalStorage::snapshotFormat(',
    'bool HalStorage::snapshotGeometry(',
    'bool HalStorage::readExFatSector(',
    'HalStorage::SnapshotRead HalStorage::readDirectoryRecord(', 'HalFile HalStorage::open(',
    'HalStorage::SnapshotRead HalStorage::readRootDirectoryRecord(',
    'HalStorage::PathStatus HalStorage::lookupPath(', 'bool HalStorage::mkdir(', 'bool HalStorage::remove(',
    'bool HalFile::captureReadCursor(', 'bool HalFile::restoreReadCursor(',
    'size_t HalFile::write(const uint8_t*', 'bool HalFile::sync()', 'bool HalFile::close()',
]
functions = '\n'.join(function(signature) for signature in signatures)
section_source = (ROOT / 'lib/Epub/Epub/Section.cpp').read_text()
def section_function(signature):
    global source
    previous = source
    try:
        source = section_source
        return function(signature)
    finally:
        source = previous
section_methods = '\n'.join(section_function(signature) for signature in
                            ('bool Section::openSpeculativeOutput()', 'bool Section::closeSpeculativeOutput()',
                             'bool Section::reopenSpeculativeOutput()', 'void Section::discardOpenBuildOutput()',
                             'bool Section::commitBuildFile('))
section_header_size = re.search(r'constexpr uint32_t HEADER_SIZE\s*=\s*.*?;', section_source, re.S).group()
page_source = (ROOT / 'lib/Epub/Epub/Page.cpp').read_text()
previous_source = source
source = page_source
page_rule_serialize = function('bool PageHorizontalRule::serialize(')
source = previous_source
fixture_spec = importlib.util.spec_from_file_location('sdmmc_fixture', ROOT / 'test/sdmmc_monitor/fixture.py')
fixture = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(fixture)
sdk = fixture.patched_source()
volume_spec = importlib.util.spec_from_file_location('volume_patcher', ROOT / 'scripts/patch_freeink_volume.py')
volume_patcher = importlib.util.module_from_spec(volume_spec)
volume_spec.loader.exec_module(volume_patcher)
sdk_header = fixture.patched_source(volume_patcher)
getter_start = sdk_header.index('uint8_t filesystemType()')
getter_end = sdk_header.index('}', getter_start) + 1
sdk_getter = sdk_header[getter_start:getter_end]
geometry_start = sdk_header.index('  struct VolumeGeometry {')
geometry_end = sdk_header.index('  // Returns the total card capacity', geometry_start)
sdk_geometry = sdk_header[geometry_start:geometry_end]
def sdk_function(signature):
    start = sdk.index(signature)
    opening = sdk.index('{', start)
    depth = 0
    for i in range(opening, len(sdk)):
        if sdk[i] == '{':
            depth += 1
        elif sdk[i] == '}':
            depth -= 1
            if not depth:
                return sdk[start:i + 1]
    raise AssertionError(signature)
monitor = sdk[sdk.index('namespace {\nstd::atomic<bool> syncMonitorArmed'):
              sdk.index('\n#endif', sdk.index('namespace {\nstd::atomic<bool> syncMonitorArmed'))]
sdk_read = sdk_function('bool SdmmcBlockDevice::readSectors(')
sdk_write = sdk_function('bool SdmmcBlockDevice::writeSectors(')
if os.environ.get('CROSSPOINT_DIAG_WRITE_FENCE') == '1':
    signature = 'bool SdmmcBlockDevice::writeSectors(Sector_t sector, const uint8_t* src, size_t ns) {'
    assert sdk_write.count(signature) == 1
    sdk_write = sdk_write.replace(signature, signature + '\n  if (!crosspoint_sdmmc_monitor_healthy()) return false;')
code = r'''
#include <FsLib/FsFile.h>
#include <FsLib/FsVolume.h>
#include <FatLib/FatFormatter.h>
#include <ExFatLib/ExFatFormatter.h>
#include <common/FsBlockDeviceInterface.h>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>
#include <csignal>
#include <unistd.h>
#include "StorageMutationFence.h"
#include "CheckedStoragePath.h"

#ifdef EXPECT_EXFAT_SEPARATE
static_assert(USE_EXFAT_BITMAP_CACHE == 1, "separate bitmap cache test configuration");
static_assert(USE_SEPARATE_FAT_CACHE == 1, "separate FAT cache test configuration");
#endif

bool failNextAllocation=false;
int failAfterAllocation=-1;
bool allocationFails(){if(std::exchange(failNextAllocation,false))return true;
 if(failAfterAllocation<0)return false;if(failAfterAllocation--==0){failAfterAllocation=-1;return true;}return false;}
template<typename T> requires std::is_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t size) {if(allocationFails())return {};return std::make_unique<T>(size);}
template<typename T,typename... A> requires (!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(A&&... args) {if(allocationFails())return {};return std::make_unique<T>(std::forward<A>(args)...);}
struct Disk : FsBlockDeviceInterface {
 std::unordered_map<Sector_t,std::array<uint8_t,512>> sectors;
 int reads=0,failAt=-1,writes=0,failWriteAt=-1,writesAfterFailure=0,writesAfterReadFailure=0,readsAfterFailure=0;bool injected=false,writeInjected=false;
 Sector_t capacity=1024*1024;
 bool isBusy() override{return false;}
 Sector_t sectorCount() override{return capacity;}
 bool syncDevice() override{return true;}
 bool readSector(Sector_t sector,uint8_t* out) override {
  if(injected)++readsAfterFailure;
  assert(sector<sectorCount());if(reads++==failAt){injected=true;return false;}
  auto it=sectors.find(sector);if(it==sectors.end())std::memset(out,0,512);else std::memcpy(out,it->second.data(),512);return true;
 }
 bool readSectors(Sector_t sector,uint8_t* out,size_t count) override {
  for(size_t i=0;i<count;++i)if(!readSector(sector+i,out+i*512))return false;return true;
 }
 bool writeSector(Sector_t sector,const uint8_t* bytes) override {
  if(writeInjected)++writesAfterFailure;
  if(injected)++writesAfterReadFailure;
  if(writes++==failWriteAt){writeInjected=true;return false;}
  assert(sector<sectorCount());std::memcpy(sectors[sector].data(),bytes,512);return true;
 }
 bool writeSectors(Sector_t sector,const uint8_t* bytes,size_t count) override {
  for(size_t i=0;i<count;++i)if(!writeSector(sector+i,bytes+i*512))return false;return true;
 }
};
''' + monitor + r'''
struct sdmmc_card_t {Disk* disk;};
constexpr int ESP_OK=0;
int sdmmc_read_sectors(sdmmc_card_t* card,void* out,Sector_t sector,size_t n){return card->disk->readSectors(sector,static_cast<uint8_t*>(out),n)?0:-1;}
int sdmmc_write_sectors(sdmmc_card_t* card,const void* in,Sector_t sector,size_t n){return card->disk->writeSectors(sector,static_cast<const uint8_t*>(in),n)?0:-1;}
namespace freeink {
struct SdmmcBlockDevice : FsBlockDeviceInterface {
 void* _card;uint8_t* _dmaBuffer;
 static constexpr size_t kSectorSize=512,kMaxTransferSectors=8;
 bool isBusy() override{return false;}
 bool readSector(Sector_t sector,uint8_t* out) override{return readSectors(sector,out,1);}
 bool readSectors(Sector_t,uint8_t*,size_t) override;
 bool writeSector(Sector_t sector,const uint8_t* in) override{return writeSectors(sector,in,1);}
 bool writeSectors(Sector_t,const uint8_t*,size_t) override;
 Sector_t sectorCount() override{return static_cast<sdmmc_card_t*>(_card)->disk->sectorCount();}
 bool syncDevice() override{return true;}
};
''' + sdk_read + '\n' + sdk_write + r'''
}
struct Card {
 FsVolume volume;bool mounted=false,initialized=false;freeink::SdmmcBlockDevice* device=nullptr;
 FsVolume& vol(){return volume;}
''' + sdk_getter + r'''
 FsBlockDeviceInterface* rawBlockDevice(){return device;}
''' + sdk_geometry + r'''
 bool begin(){mounted=volume.begin(device);initialized=mounted;return mounted;}
 bool ready()const{return mounted;}
 FsFile open(const char* path,oflag_t mode=O_RDONLY){return volume.open(path,mode);}
 bool exists(const char* path){return volume.exists(path);}
 bool mkdir(const char* path,bool recursive){return volume.mkdir(path,recursive);}
 bool remove(const char* path){return volume.remove(path);}
} SDCard;
using SDCardManager=Card;
class HalStorage;
class HalFile {
 public:
 struct ReadCursor{uint64_t position=0;uint32_t cluster=0;};
 class Impl;std::unique_ptr<Impl> impl;
 HalFile();explicit HalFile(std::unique_ptr<Impl>);~HalFile();HalFile(HalFile&&);HalFile& operator=(HalFile&&);
 size_t write(const uint8_t*,size_t);int read(void*,size_t);bool sync();bool close();
 size_t getName(char*,size_t);
 bool isDirectory() const;uint64_t fileSize64();bool seek64(uint64_t);bool seek(size_t);size_t position()const;
#ifdef EXPERIMENT_LEGACY_CLOSE
 int available()const;size_t size()const;
#endif
 bool captureReadCursor(ReadCursor&);bool restoreReadCursor(const ReadCursor&);
#ifdef EXPERIMENT_READ_DISCARD
 bool experimentalDiscardReadOnly();
#endif
 explicit operator bool() const;
};
class HalStorage {
 friend class HalFile;checkedscan::StorageMutationFence mutationFence;
 public:
 enum class PathStatus {Missing,Present,Failed};
 enum class SnapshotRead {Record,End,Changed,Failed};
 enum class SnapshotFormat {Unknown,Fat,ExFat};
 enum class ExFatSector {Metadata,Fat,Bitmap};
 struct VolumeGeometry{uint8_t type=0;uint32_t deviceSectors=0,fatStart=0,dataStart=0,clusterCount=0,clusterBytes=0;};
 using MutationSnapshot=checkedscan::StorageMutationFence::Snapshot;
 using MutationMarker=checkedscan::StorageMutationFence::Marker;
 class RootDirectoryCursor{friend class HalStorage;MutationSnapshot snapshot{};uint64_t position=0;
  uint32_t cluster=0;bool started=false,failed=false;};
 class StorageLock {static std::recursive_mutex mutex;std::lock_guard<std::recursive_mutex> guard;
 public:StorageLock():guard(mutex){}};
 static HalStorage& getInstance(){static HalStorage instance;return instance;}
 bool begin();
 bool exists(const char* path){StorageLock lock;return SDCard.exists(path);}
 bool beginMutationSnapshot(MutationSnapshot&);bool mutationSnapshotUnchanged(const MutationSnapshot&);
 bool monitoredStorageHealthy();bool withStorageLock(bool (*)(void*),void*);
 bool captureMutationMarker(MutationMarker&);bool mutationMarkerUnchanged(const MutationMarker&);
 SnapshotFormat snapshotFormat(const MutationSnapshot&);
 bool snapshotGeometry(const MutationSnapshot&,VolumeGeometry&);
 bool readExFatSector(const MutationSnapshot&,ExFatSector,uint32_t,uint8_t*);
 SnapshotRead readDirectoryRecord(HalFile&,const MutationSnapshot&,uint8_t*);
 SnapshotRead readRootDirectoryRecord(const MutationSnapshot&,RootDirectoryCursor&,uint8_t*);
 HalFile open(const char*,oflag_t=O_RDONLY);PathStatus lookupPath(const char*);bool mkdir(const char*,bool);bool remove(const char*);
 bool rename(const char*,const char*){assert(false && "speculative commit must not rename");return false;}
};
std::recursive_mutex HalStorage::StorageLock::mutex;
''' + impl + '\n' + constructors + '\n' + functions + r'''
#ifdef EXPERIMENT_READ_DISCARD
bool HalFile::experimentalDiscardReadOnly(){
 HalStorage::StorageLock lock;
 if(!impl || impl->hasWriterLease)return false;
 const int result=impl->file.experimentalDiscardReadOnly();
 if(result==2)HalStorage::getInstance().mutationFence.poison();
 return result==1;
}
#endif
HalFile::operator bool() const {return impl && impl->file.isOpen();}
bool HalFile::isDirectory() const {return impl && impl->file.isDirectory();}
uint64_t HalFile::fileSize64(){return impl->file.fileSize();}
size_t HalFile::getName(char* out,size_t size){return impl->file.getName(out,size);}
bool HalFile::seek64(uint64_t pos){return impl->file.seekSet(pos);}
bool HalFile::seek(size_t pos){return impl->file.seekSet(pos);}
size_t HalFile::position()const{return impl->file.curPosition();}
#ifdef EXPERIMENT_LEGACY_CLOSE
int HalFile::available()const{return impl->file.available();}
size_t HalFile::size()const{return impl->file.fileSize();}
#endif
int HalFile::read(void* buffer,size_t count){HalStorage::StorageLock lock;assert(impl);return impl->file.read(buffer,count);}
#define Storage HalStorage::getInstance()
#define LOG_ERR(...) ((void)0)
constexpr uint8_t SECTION_FILE_VERSION=78,SECTION_FILE_PARTIAL_VERSION=0xFE - (SECTION_FILE_VERSION-28);
''' + section_header_size + r'''
namespace serialization {
template<typename T>bool tryWritePod(HalFile& f,const T& value){return f.write(reinterpret_cast<const uint8_t*>(&value),sizeof(value))==sizeof(value);}
template<typename T>bool tryReadPod(HalFile& f,T& value){return f.read(&value,sizeof(value))==sizeof(value);}
template<typename T>void writePod(HalFile& f,const T& value){(void)tryWritePod(f,value);}
bool tryWriteString(HalFile& f,const std::string& value){uint32_t n=value.size();return tryWritePod(f,n) && (!n || f.write(reinterpret_cast<const uint8_t*>(value.data()),n)==n);}
}
struct PageHorizontalRule{int16_t xPos=1,yPos=1;uint16_t width=5;uint8_t thickness=1;bool serialize(HalFile&);};
''' + page_rule_serialize + r'''
struct Section {
 HalFile file;HalStorage::MutationMarker speculativeMarker_;uint64_t speculativePosition_=0,speculativeSize_=0;
 uint16_t speculativeMaxPages_=32,builtPageCount_=0;bool preserveSpeculativeOutput_=false;
 struct PageEntry{uint32_t fileOffset;uint16_t paragraphIndex,listItemIndex;uint32_t visibleTextOffset;};
 struct Parser{std::vector<std::pair<std::string,uint16_t>> anchors;const auto& getAnchors(){return anchors;}};
 struct Context{std::vector<PageEntry> lut;std::unique_ptr<Parser> parser=std::make_unique<Parser>();};
 std::unique_ptr<Context> build_=std::make_unique<Context>();
 std::string filePath="/__xsync_0123456789abcdef0123456789abcdef.part";
 std::string speculativeFilePath()const{return filePath+".prefetch";}
 std::string binTmpPath()const{return speculativeFilePath();}
 bool openSpeculativeOutput();bool closeSpeculativeOutput();bool reopenSpeculativeOutput();
 void discardOpenBuildOutput();bool commitBuildFile(uint8_t,uint32_t,uint32_t);
};
''' + section_methods + r'''
#ifdef EXPERIMENT_LEGACY_CLOSE
#include "CrossInkBookmarks.cpp"
#endif
#include "StorageDirectoryScan.h"
#include "InventorySession.h"
#include "ExclusiveStage.h"
#include "RawStageSession.h"
#include "StageHash.h"
#include "FreeClusterCounter.h"
#include "FreeSpaceScan.h"
#include "ReservedStage.h"
#include "ExFatBitmapLayout.h"
#include "ExFatLayoutScan.h"
#include "ExFatReservedStage.h"
#include "ExFatStageOwner.h"
#include "GuardedRawStageSession.h"
#include "GuardedRawStagePump.h"
#include "GuardedRawStageSocket.h"
#include "GuardedRawStageConnection.h"
#include "GuardedRawStageListener.h"
#include "ExFatBootCheck.h"
#include "PublicationIntentStore.h"
struct ScanState {unsigned entries=0;unsigned excluded=0;bool running=true;};
bool keepGoing(void* state){return static_cast<ScanState*>(state)->running;}
bool emitEntry(void* state,const wirecheck::DirectoryEntry&){++static_cast<ScanState*>(state)->entries;return true;}
bool emitIncluded(void* state,const wirecheck::DirectoryEntry& entry){
 auto* s=static_cast<ScanState*>(state);
 if(wirecheck::InventorySession::includedName(entry.name))++s->entries;else ++s->excluded;return true;
}
int main(int argc,char** argv) {
 assert(argc==4);const bool exfat=std::string(argv[1])=="exfat";const std::string test=argv[2];const int fault=std::stoi(argv[3]);
 Disk disk;uint8_t buffer[512]{};
 if(test=="space-fat32-high")disk.capacity=0x420000;
 if(exfat && (test=="allocation-cache-eviction-fault" || test=="allocation-cache-read-fault"))disk.capacity=0x420000;
 if(exfat){ExFatFormatter formatter;assert(formatter.format(&disk,buffer));}
 else {FatFormatter formatter;assert(formatter.format(&disk,buffer));}
 sdmmc_card_t card{&disk};std::array<uint8_t,4096> dma{};freeink::SdmmcBlockDevice device;
 device._card=&card;device._dmaBuffer=dma.data();
 if(test=="mount") {
  SDCard.device=&device;disk.reads=0;disk.failAt=fault;
  auto& storage=HalStorage::getInstance();bool mounted=storage.begin();
  HalStorage::MutationSnapshot snapshot;bool available=storage.beginMutationSnapshot(snapshot);
  if(disk.injected)assert(!available);
  if(mounted && !disk.injected)assert(available);
  std::printf("injected=%d mounted=%d available=%d\n",disk.injected,mounted,available);return 0;
 }
 assert(SDCard.volume.begin(&device));SDCard.mounted=true;SDCard.initialized=true;
 assert(SDCard.volume.mkdir("/Books"));
 FsFile books=SDCard.volume.open("/Books",O_RDONLY);assert(books && books.isDirectory());
 for(int i=0;i<40;++i){FsFile file;const auto name=std::to_string(i)+" unique book.epub";if(!file.open(&books,name.c_str(),O_CREAT|O_WRONLY)){std::fprintf(stderr,"create failed index=%d error=%d\n",i,books.getError());return 5;}assert(file.write("x",1)==1);assert(file.close());}
 assert(books.close());
 assert(SDCard.volume.mkdir("/Books/zz cache"));
 if(test=="root-cursor")for(int i=0;i<100;++i){auto f=SDCard.volume.open(("/ROOT-"+std::to_string(i)+".TMP").c_str(),O_CREAT|O_WRONLY);assert(f && f.close());}
 assert(SDCard.volume.mkdir("/.crosspoint"));assert(SDCard.volume.mkdir("/XTCache"));
 assert(SDCard.volume.mkdir("/System Volume Information"));
 {auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_CREAT|O_WRONLY);
  assert(state && state.write("state",5)==5 && state.close());}
 const char* stageName="__xsync_0123456789abcdef0123456789abcdef.part";
 if(test=="hash-directory-reject") {
  assert(SDCard.volume.mkdir((std::string("/")+stageName).c_str()));
 } else if(test=="hash-fragmented" || test=="hash-position-cursor" || test=="hash-position-seek") {
  auto stage=SDCard.volume.open((std::string("/")+stageName).c_str(),O_CREAT|O_WRONLY);
  assert(stage);const auto cluster=SDCard.volume.bytesPerCluster();std::vector<uint8_t> payload(cluster);
  for(int i=0;i<12;++i) {
   for(size_t j=0;j<payload.size();++j)payload[j]=static_cast<uint8_t>((i*37+j*13+j/251)%251);
   const size_t count=i==11?payload.size()-137:payload.size();
   assert(stage.write(payload.data(),count)==count && stage.sync());
   const auto filler=std::string("/Books/F")+std::to_string(i)+".TMP";
   auto f=SDCard.volume.open(filler.c_str(),O_CREAT|O_WRONLY);
   std::fill(payload.begin(),payload.end(),0xe9);
   assert(f && f.write(payload.data(),payload.size())==payload.size() && f.close());
  }
  assert(stage.close());auto verify=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
  Sector_t first=0,last=0;assert(verify && !verify.contiguousRange(&first,&last) && verify.close());
 } else if(test.rfind("hash-",0)==0 || test.rfind("read-discard-",0)==0) {
  auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_CREAT|O_WRONLY);
  std::array<uint8_t,1024> payload{};for(size_t i=0;i<payload.size();++i)payload[i]=i%251;
  assert(f && f.write(payload.data(),payload.size())==payload.size() && f.close());
 }
 if(test=="stage-collision" || test=="raw-collision") {auto f=SDCard.volume.open("/__xsync_0123456789ABCDEF0123456789ABCDEF.part",O_CREAT|O_WRONLY);
  assert(f && f.write("old",3)==3 && f.close());}
 int32_t fatFreeBefore=-1;
 if(test=="space-fat32-high") {
  assert(!exfat && SDCard.volume.fatType()==32);fatFreeBefore=SDCard.volume.freeClusterCount();assert(fatFreeBefore>0);
  const uint32_t cluster=1000;const auto sector=SDCard.volume.fatStartSector()+cluster/128;const size_t offset=(cluster%128)*4;
  SDCard.volume.end();std::array<uint8_t,512> sectorData{};assert(device.readSector(sector,sectorData.data()));
  assert(sectorData[offset]==0 && sectorData[offset+1]==0 && sectorData[offset+2]==0 && sectorData[offset+3]==0);
  sectorData[offset+3]=0xf0;assert(device.writeSector(sector,sectorData.data()) && SDCard.volume.begin(&device));
 }
 if(test.rfind("exfat-bitmap-layout-",0)==0 && test!="exfat-bitmap-layout-read-fault" &&
    test!="exfat-bitmap-layout-mutation" && test!="exfat-bitmap-layout-cancel" &&
    (test.rfind("exfat-bitmap-layout-stage",0)!=0 || test=="exfat-bitmap-layout-stage-bad-layout" ||
     test=="exfat-bitmap-layout-stage-bitmap-free")) {
  assert(exfat);const auto rootSector=SDCard.volume.dataStartSector()+2*(SDCard.volume.bytesPerCluster()/512);
  const auto fatSector=SDCard.volume.fatStartSector(),bitmapSector=SDCard.volume.dataStartSector();SDCard.volume.end();
  std::array<uint8_t,512> sector{};
  if(test=="exfat-bitmap-layout-chain") {
   assert(device.readSector(fatSector,sector.data()));sector[8]=0;
   assert(device.writeSector(fatSector,sector.data()));
  } else if(test=="exfat-bitmap-layout-stage-bitmap-free") {
   assert(device.readSector(bitmapSector,sector.data()) && (sector[0]&1));sector[0]&=uint8_t(~1);
   assert(device.writeSector(bitmapSector,sector.data()));
  } else {
   assert(device.readSector(rootSector,sector.data()) && sector[32]==0x81);
   if(test=="exfat-bitmap-layout-relocated" || test=="exfat-bitmap-layout-stage-bad-layout") sector[32+20]=3;
   else if(test=="exfat-bitmap-layout-length") sector[32+24]^=1;
   else if(test=="exfat-bitmap-layout-flags") sector[32+1]=1;
   else if(test=="exfat-bitmap-layout-missing") sector[32]=1;
   else if(test=="exfat-bitmap-layout-duplicate") std::copy_n(sector.data()+32,32,sector.data()+64);
   else assert(false);
   assert(device.writeSector(rootSector,sector.data()));
  }
  assert(SDCard.volume.begin(&device));
 }
 if(test=="exfat-boot-two-fats" || test=="exfat-bitmap-layout-stage-bad-boot" || test=="owner-stage-bad-boot" ||
    test=="exfat-boot-dirty" || test=="exfat-boot-bad-checksum") {
  assert(exfat);SDCard.volume.end();std::array<uint8_t,512> data{};
  assert(device.readSector(0,data.data()));const auto start=uint32_t(data[454])|(uint32_t(data[455])<<8)|
   (uint32_t(data[456])<<16)|(uint32_t(data[457])<<24);assert(start>0);
  if(test!="exfat-boot-bad-checksum") {
   assert(device.readSector(start,data.data()));
   if(test=="exfat-boot-two-fats" || test=="exfat-bitmap-layout-stage-bad-boot" || test=="owner-stage-bad-boot")data[110]=2;else data[106]|=2;
   assert(device.writeSector(start,data.data()));
   uint32_t checksum=0;for(uint32_t n=0;n<11;++n){assert(device.readSector(start+n,data.data()));
    for(unsigned i=0;i<512;++i){if(n==0 && (i==106 || i==107 || i==112))continue;
     checksum=((checksum>>1)|(checksum<<31))+data[i];}}
   for(unsigned i=0;i<512;i+=4)for(unsigned b=0;b<4;++b)data[i+b]=uint8_t(checksum>>(8*b));
   assert(device.writeSector(start+11,data.data()));
  } else {assert(device.readSector(start+1,data.data()));data[7]^=1;assert(device.writeSector(start+1,data.data()));}
  assert(SDCard.volume.begin(&device));
 }
 SDCard.volume.end();assert(SDCard.volume.begin(&device));crosspoint_sdmmc_monitor_arm();
 auto& storage=HalStorage::getInstance();HalStorage::MutationSnapshot snapshot;assert(storage.beginMutationSnapshot(snapshot));
#ifdef EXPERIMENT_LEGACY_CLOSE
 if(test=="legacy-empty-close") {
  const std::string book="/Books/absent.epub";CrossInkBookmarks absent;
  assert(!absent.open(book) && !absent.unreadable());absent.close();
  assert(SDCard.volume.mkdir("/.crosspoint/bookmarks"));
  const auto path=std::string("/.crosspoint/bookmarks/epub_")+std::to_string(credential_integrity::crc32(book))+".bin";
  auto old=storage.open(path.c_str(),O_CREAT|O_WRONLY);const uint8_t byte=5;assert(old && old.write(&byte,1)==1 && old.close());
  CrossInkBookmarks failed;failAfterAllocation=1;
  assert(!failed.open(book) && failed.unreadable() && failAfterAllocation==-1);failed.close();
  SDCard.volume.end();assert(SDCard.volume.begin(&device));auto file=SDCard.volume.open(path.c_str(),O_RDONLY);
  assert(file && file.read()==byte && file.close());std::puts("legacy_empty_close=pass");return 0;
 }
 if(test=="legacy-lookup-fault") {
  SDCard.volume.end();assert(SDCard.volume.begin(&device));disk.failAt=disk.reads;
  CrossInkBookmarks bookmarks;const int writes=disk.writes;
  assert(!bookmarks.open("/Books/missing.epub") && bookmarks.unreadable() && disk.injected);
  assert(disk.writes==writes);disk.failAt=-1;bookmarks.close();
  SDCard.volume.end();assert(SDCard.volume.begin(&device));auto file=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);
  char state[5]{};assert(file && file.read(state,5)==5 && std::memcmp(state,"state",5)==0 && file.close());
  std::puts("legacy_lookup_fault=pass");return 0;
 }
 if(test=="legacy-read-sweep") {
  const std::string book="/Books/test.epub";
  const auto first=std::string("/.crosspoint/bookmarks/epub_")+std::to_string(credential_integrity::crc32(book))+".bin";
  const auto second=std::string("/.crosspoint/bookmarks/epub_")+std::to_string(std::hash<std::string>{}(book))+".bin";
  assert(first!=second && SDCard.volume.mkdir("/.crosspoint/bookmarks"));
  std::vector<uint8_t> data;auto append=[&](const auto& value){auto p=reinterpret_cast<const uint8_t*>(&value);data.insert(data.end(),p,p+sizeof(value));};
  auto str=[&](const std::string& value){uint32_t n=value.size();append(n);data.insert(data.end(),value.begin(),value.end());};
  append(uint8_t{5});append(uint16_t{1});str("title");str("author");str(book);
  append(uint16_t{1});append(float{0.5f});append(uint32_t{123});data.insert(data.end(),48,0);
  append(uint16_t{0});data.insert(data.end(),64,0);
  for(const auto& path:{first,second}){auto f=storage.open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY);
   assert(f && f.write(data.data(),data.size())==data.size() && f.close());}
  SDCard.volume.end();assert(SDCard.volume.begin(&device));disk.reads=0;disk.failAt=fault;
  CrossInkBookmarks bookmarks;const bool found=bookmarks.open(book);const bool injected=disk.injected;
  if(injected){assert(bookmarks.unreadable());const int before=disk.reads;CrossInkBookmarks::Entry entry;
   assert(!bookmarks.read(0,entry) && disk.reads==before);}
  else assert(found && !bookmarks.unreadable() && bookmarks.count()==2);
  const int laterReads=disk.readsAfterFailure;
  disk.failAt=-1;bookmarks.close();SDCard.volume.end();assert(SDCard.volume.begin(&device));
  for(const auto& path:{first,second}) {auto f=SDCard.volume.open(path.c_str(),O_RDONLY);
   std::vector<uint8_t> copy(data.size());assert(f && f.read(copy.data(),copy.size())==int(copy.size()) && copy==data && f.close());}
  auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char bytes[5]{};
  assert(state && state.read(bytes,5)==5 && std::memcmp(bytes,"state",5)==0 && state.close());
  std::printf("injected=%d found=%d reads_after_failure=%d artifacts_preserved=1\n",injected,found,laterReads);return 0;
 }
#endif
 if(test=="checked-read-open-allocation") {
  const int before=disk.writes;failNextAllocation=true;
  auto failed=storage.open("/.crosspoint/progress.dat",O_RDONLY);
  assert(!failed && !failNextAllocation && disk.writes==before && storage.monitoredStorageHealthy());
  auto opened=storage.open("/.crosspoint/progress.dat",O_RDONLY);char data[5]{};
  assert(opened && opened.read(data,5)==5 && std::memcmp(data,"state",5)==0 && opened.close());
  assert(disk.writes==before);std::puts("checked_read_open_allocation=pass");return 0;
 }
 if(test=="checked-read-monitor-after-fault") {
  auto writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t b=0x47;assert(writer.write(&b,1)==1);
  disk.failWriteAt=disk.writes;assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;
  const int reads=disk.reads,writes=disk.writes;
  if(storage.monitoredStorageHealthy()) {auto reader=storage.open("/.crosspoint/progress.dat",O_RDONLY);assert(reader.close());}
  assert(disk.reads==reads && disk.writes==writes);
  const bool closed=writer.close();
  std::printf("latched_monitor_precheck_no_io=pass writer_close=%d later_writes=%d\n",closed,disk.writes-writes);return 0;
 }
 if(test.rfind("intent-store",0)==0) {
  using Store=wirecheck::publication::IntentStore;using namespace wirecheck::publication;
  Store store(storage);std::vector<uint8_t> scratch(MAX_RECORD);auto intent=std::make_unique<Intent>();
  View view{stageName,"/Authors/Writer/Book.epub",4,{1,2,3,4}};
  assert(store.load(*intent,scratch.data(),scratch.size())==Store::Load::Missing);
  auto invalid=view;invalid.destination="/Authors/Invalid:Book.epub";
  assert(store.prepare(invalid,scratch.data(),scratch.size())==Store::Prepare::Invalid);
  assert(store.load(*intent,scratch.data(),scratch.size())==Store::Load::Missing);
  if(test=="intent-store-sweep-read" || test=="intent-store-sweep-write") {
   SDCard.volume.end();assert(SDCard.volume.begin(&device));
   const int startReads=disk.reads,startWrites=disk.writes;
   if(fault>=0) {if(test=="intent-store-sweep-read")disk.failAt=startReads+fault;
                 else disk.failWriteAt=startWrites+fault;}
   const auto prepared=store.prepare(view,scratch.data(),scratch.size());
   const bool injected=test=="intent-store-sweep-read"?disk.injected:disk.writeInjected;
   const int reads=disk.reads-startReads,writes=disk.writes-startWrites;
   if(injected) {assert(prepared==Store::Prepare::Unavailable || prepared==Store::Prepare::Uncertain);
    const int before=disk.writes;assert(store.prepare(view,scratch.data(),scratch.size())==Store::Prepare::Unavailable);
    assert(disk.writes==before);}
   else assert(prepared==Store::Prepare::Prepared);
#ifdef EXPERIMENT_WRITE_FENCE
   if(injected)assert(disk.writesAfterFailure==0 && disk.writesAfterReadFailure==0);
#endif
   disk.failAt=-1;disk.failWriteAt=-1;
   SDCard.volume.end();assert(SDCard.volume.begin(&device));
   auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
   assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
   std::printf("injected=%d reads=%d writes=%d writes_after_failure=%d result=%d\n",injected,reads,writes,disk.writesAfterFailure,int(prepared));return 0;
  }
  if(test=="intent-store-write-fault")disk.failWriteAt=disk.writes;
  const auto result=store.prepare(view,scratch.data(),scratch.size());
  if(test=="intent-store-write-fault") {
   assert(disk.writeInjected && result==Store::Prepare::Uncertain);disk.failWriteAt=-1;
   assert(store.load(*intent,scratch.data(),scratch.size())==Store::Load::Unavailable);
  } else {
   assert(result==Store::Prepare::Prepared && store.load(*intent,scratch.data(),scratch.size())==Store::Load::Valid);
   assert(std::string(intent->stage.data())==view.stage && std::string(intent->destination.data())==view.destination &&
    intent->size==view.size && intent->sha256==view.sha256);
   const int writes=disk.writes;assert(store.prepare(view,scratch.data(),scratch.size())==Store::Prepare::Existing && disk.writes==writes);
   if(test=="intent-store-read-fault") {SDCard.volume.end();assert(SDCard.volume.begin(&device));
    disk.failAt=disk.reads;intent->size=987;
    assert(store.load(*intent,scratch.data(),scratch.size())==Store::Load::Unavailable && disk.injected && intent->size==987);
    disk.failAt=-1;}
   if(test=="intent-store-corrupt") {auto f=storage.open(Store::PATH,O_WRONLY);const uint8_t b=0;
    assert(f && f.seek(0) && f.write(&b,1)==1 && f.close());intent->size=987;
    assert(store.load(*intent,scratch.data(),scratch.size())==Store::Load::Invalid && intent->size==987);
    assert(store.prepare(view,scratch.data(),scratch.size())==Store::Prepare::Existing);}
  }
  SDCard.volume.end();assert(SDCard.volume.begin(&device));auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);
  char progress[5]{};assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("publication_intent_store=pass");return 0;
 }
 if(test.rfind("guarded-listener",0)==0) {
  assert(exfat);SDCard.device=&device;
  struct Context{uint32_t now=0;bool active=true;};Context context;
  auto now=[](void* p){return static_cast<Context*>(p)->now;};
  auto active=[](void* p){return static_cast<Context*>(p)->active;};
  auto identity=[](void*,const char* id){return std::string(id)==std::string(64,'a');};
  std::atomic_flag cancelled{};using Listener=wirecheck::GuardedRawStageListener;
  Listener listener(storage,64U*1024*1024,{&context,now,active,identity},cancelled);assert(listener.begin(0));
  int client=socket(AF_INET,SOCK_STREAM,0);assert(client>=0);sockaddr_in address{};
  address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(listener.localPort());
  assert(connect(client,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
  const std::string input="PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: 4\r\nContent-Type: application/octet-stream\r\n"
   "X-Xsync-Device: "+std::string(64,'a')+"\r\nX-Xsync-Stage: "+stageName+"\r\n\r\nABCD";
  const size_t sendSize=test=="guarded-listener-stop-after-create"?input.size()-4:input.size();
  assert(send(client,input.data(),sendSize,0)==static_cast<ssize_t>(sendSize));
  if(test=="guarded-listener-cancel") {
   unsigned attempts=0;while(!listener.busy()){assert(++attempts<10000 && !listener.completed());listener.poll();}
   cancelled.test_and_set();listener.poll();
   assert(!listener.running() && !listener.busy() && listener.completed() &&
    listener.result()==wirecheck::GuardedRawStageConnection::Result::ResponseLost &&
    !SDCard.volume.exists((std::string("/")+stageName).c_str()));
   close(client);std::puts("guarded_listener_cancel=pass");return 0;
  }
  if(test=="guarded-listener-stop-after-create") {
   unsigned count=0;while(!SDCard.volume.exists((std::string("/")+stageName).c_str())) {
    assert(++count<10000 && !listener.completed());++context.now;listener.poll();}
   assert(listener.busy());listener.stop();assert(listener.completed() &&
    listener.result()==wirecheck::GuardedRawStageConnection::Result::ResponseLost &&
    SDCard.volume.exists((std::string("/")+stageName).c_str()));
   close(client);std::puts("guarded_listener_stop_preserves_ambiguous_stage=pass");return 0;
  }
  unsigned polls=0;while(!listener.completed()){assert(++polls<10000);++context.now;listener.poll();}
  assert(listener.result()==wirecheck::GuardedRawStageConnection::Result::Staged && !listener.busy());
  char reply[256]{};const auto n=recv(client,reply,sizeof(reply),0);assert(n>0);
  assert(std::string(reply,static_cast<size_t>(n)).ends_with("{\"staged\":true}"));
  const auto port=listener.localPort();close(client);listener.stop();assert(!listener.running());
  assert(listener.begin(port));listener.stop();SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);char actual[4]{};
  assert(f && f.read(actual,4)==4 && std::memcmp(actual,"ABCD",4)==0 && f.close());
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("guarded_listener_stage=pass");return 0;
 }
 if(test.rfind("guarded-connection",0)==0) {
  assert(exfat);SDCard.device=&device;int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
  struct Context{uint32_t now=0;bool active=true;bool identity=true;};Context context;
  context.identity=test!="guarded-connection-reject";
  auto now=[](void* p){return static_cast<Context*>(p)->now;};
  auto active=[](void* p){return static_cast<Context*>(p)->active;};
  auto identity=[](void* p,const char* id){return static_cast<Context*>(p)->identity && std::string(id)==std::string(64,'a');};
  std::array<uint8_t,3> buffer{};std::atomic_flag cancelled{};using Connection=wirecheck::GuardedRawStageConnection;
  auto receiver=std::make_unique<Connection>(storage,sockets[0],buffer.data(),buffer.size(),64U*1024*1024,
   wirecheck::GuardedRawStageSocket::Callbacks{&context,now,active,identity},cancelled,0);
  const std::string input="PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: 4\r\nContent-Type: application/octet-stream\r\n"
   "X-Xsync-Device: "+std::string(64,'a')+"\r\nX-Xsync-Stage: "+stageName+"\r\n\r\nABCD";
  assert(send(sockets[1],input.data(),input.size(),0)==static_cast<ssize_t>(input.size()));
  if(test=="guarded-connection-lost"){assert(close(sockets[1])==0);sockets[1]=-1;}
  if(test=="guarded-connection-cancel")cancelled.test_and_set();
  auto result=Connection::Result::Pending;unsigned polls=0;
  while(result==Connection::Result::Pending){assert(++polls<10000);++context.now;result=receiver->poll();}
  if(test=="guarded-connection-cancel") {
   assert(result==Connection::Result::ResponseLost && !SDCard.volume.exists((std::string("/")+stageName).c_str()));
  } else if(test=="guarded-connection-lost") {
   assert(result==Connection::Result::ResponseLost);
  } else {
   char response[256]{};const auto n=recv(sockets[1],response,sizeof(response),0);assert(n>0);
   const std::string reply(response,static_cast<size_t>(n));
   if(test=="guarded-connection-reject")assert(result==Connection::Result::Rejected && reply.find("400 Bad Request")!=std::string::npos);
   else assert(result==Connection::Result::Staged && reply.find("201 Created")!=std::string::npos && reply.ends_with("{\"staged\":true}"));
  }
  receiver.reset();close(sockets[0]);if(sockets[1]>=0)close(sockets[1]);
  if(test!="guarded-connection-reject" && test!="guarded-connection-cancel") {
   SDCard.volume.end();assert(SDCard.volume.begin(&device));auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
   char actual[4]{};assert(f && f.read(actual,4)==4 && std::memcmp(actual,"ABCD",4)==0 && f.close());}
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("guarded_connection=pass");return 0;
 }
 if(test=="guarded-socket") {
  assert(exfat);SDCard.device=&device;int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
  struct Context{uint32_t now=0;bool active=true;};Context context;
  auto now=[](void* p){return static_cast<Context*>(p)->now;};
  auto active=[](void* p){return static_cast<Context*>(p)->active;};
  auto identity=[](void*,const char* id){return std::string(id)==std::string(64,'a');};
  std::array<uint8_t,3> buffer{};std::atomic_flag cancelled{};using Socket=wirecheck::GuardedRawStageSocket;
  auto receiver=std::make_unique<Socket>(storage,sockets[0],buffer.data(),buffer.size(),64U*1024*1024,
   Socket::Callbacks{&context,now,active,identity},cancelled,0);
  const std::string input="PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: 4\r\nContent-Type: application/octet-stream\r\n"
   "X-Xsync-Device: "+std::string(64,'a')+"\r\nX-Xsync-Stage: "+stageName+"\r\n\r\nABCD";
  assert(send(sockets[1],input.data(),input.size(),0)==static_cast<ssize_t>(input.size()));
  auto result=wirecheck::GuardedRawStagePump::Result::Pending;unsigned polls=0;
  while(result==wirecheck::GuardedRawStagePump::Result::Pending){assert(++polls<10000);++context.now;result=receiver->poll();}
  assert(result==wirecheck::GuardedRawStagePump::Result::Complete);receiver.reset();close(sockets[0]);close(sockets[1]);
  SDCard.volume.end();assert(SDCard.volume.begin(&device));auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
  char actual[4]{};assert(f && f.read(actual,4)==4 && std::memcmp(actual,"ABCD",4)==0 && f.close());
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("guarded_socket_stage=pass");return 0;
 }
 if(test.rfind("guarded-pump",0)==0) {
  assert(exfat);SDCard.device=&device;
  struct Context{bool active=true;uint32_t now=0;std::string input;size_t offset=0;unsigned reads=0;};Context context;
  const std::string body="ABCD";
  context.input="PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: 4\r\nContent-Type: application/octet-stream\r\n"
   "X-Xsync-Device: "+std::string(64,'a')+"\r\nX-Xsync-Stage: "+stageName+"\r\n\r\n"+body;
  if(test=="guarded-pump-short")context.input.pop_back();
  auto now=[](void* p){return static_cast<Context*>(p)->now;};
  auto active=[](void* p){return static_cast<Context*>(p)->active;};
  auto identity=[](void*,const char* id){return std::string(id)==std::string(64,'a');};
  auto read=[](void* p,uint8_t* out,size_t cap){auto& c=*static_cast<Context*>(p);++c.reads;
   const auto n=std::min(cap,c.input.size()-c.offset);if(!n)return -1;
   std::memcpy(out,c.input.data()+c.offset,n);c.offset+=n;return static_cast<int>(n);};
  std::atomic_flag cancelled{};std::array<uint8_t,3> buffer{};using Pump=wirecheck::GuardedRawStagePump;
  auto pump=std::make_unique<Pump>(storage,buffer.data(),buffer.size(),64U*1024*1024,
    Pump::Callbacks{&context,now,read,active,identity},cancelled,0);
  if(test=="guarded-pump-cancel-idle" || test=="guarded-pump-cancel-partial") {
   if(test=="guarded-pump-cancel-partial")assert(pump->poll()==Pump::Result::Pending && context.reads==1);
   const auto reads=context.reads;const auto writes=disk.writes;cancelled.test_and_set();
   assert(pump->poll()==Pump::Result::Failed && context.reads==reads && disk.writes==writes);
   assert(!SDCard.volume.exists((std::string("/")+stageName).c_str()));
   std::puts("guarded_pump_header_cancel=pass");return 0;
  }
  auto result=Pump::Result::Pending;unsigned polls=0;
  while(result==Pump::Result::Pending){assert(++polls<10000);
   context.now+=test=="guarded-pump-slow"?2000:1;result=pump->poll();}
  if(test=="guarded-pump-short") {assert(result==Pump::Result::Failed);return 0;}
  assert(result==Pump::Result::Complete && context.offset==context.input.size());
  if(test=="guarded-pump-slow")assert(context.now>30000);
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);char actual[4]{};
  assert(f && f.read(actual,4)==4 && std::memcmp(actual,body.data(),4)==0 && f.close());
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::printf("guarded_pump=pass polls=%u reads=%u elapsed=%u\n",polls,context.reads,context.now);return 0;
 }
 if(test.rfind("guarded-stage",0)==0) {
  assert(exfat);SDCard.device=&device;struct Context{bool active=true;bool identity=true;};Context context;
  auto active=[](void* p){return static_cast<Context*>(p)->active;};
  auto identity=[](void* p,const char* id){return static_cast<Context*>(p)->identity && std::string(id)==std::string(64,'a');};
  std::atomic_flag cancelled{};std::array<uint8_t,3> buffer{};using Session=wirecheck::GuardedRawStageSession;
  auto session=std::make_unique<Session>(storage,buffer.data(),buffer.size(),64U*1024*1024,
    Session::Callbacks{&context,active,identity},cancelled);
  const bool empty=test=="guarded-stage-empty";const std::string body=empty?"":"ABCD";
  const std::string header="PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: "+std::to_string(body.size())+
    "\r\nContent-Type: application/octet-stream\r\nX-Xsync-Device: "+std::string(64,'a')+
    "\r\nX-Xsync-Stage: "+stageName+"\r\n\r\n";
  if(test=="guarded-stage-identity")context.identity=false;
  if(test=="guarded-stage-flag-header")cancelled.test_and_set();
  const std::string input=header+body+"tail";const auto used=session->consume(reinterpret_cast<const uint8_t*>(input.data()),input.size());
  if(test=="guarded-stage-flag-header") {assert(used==0 && session->state()==Session::State::Failed);return 0;}
  assert(used==header.size() && !SDCard.volume.exists((std::string("/")+stageName).c_str()));
  if(test=="guarded-stage-identity") {assert(session->state()==Session::State::Failed);return 0;}
  assert(session->state()==Session::State::Preparing && session->consume(reinterpret_cast<const uint8_t*>(body.data()),body.size())==0);
  if(test=="guarded-stage-cancel")session->cancel();
  if(test=="guarded-stage-stale") {auto f=storage.open("/Books/stage-change.tmp",O_CREAT|O_WRONLY);
   const uint8_t b=1;assert(f && f.write(&b,1)==1 && f.close());}
  auto state=session->step();unsigned steps=0;while(state==Session::State::Preparing){assert(++steps<10000);state=session->step();}
  if(test=="guarded-stage-cancel" || test=="guarded-stage-stale") {
   assert(state==Session::State::Failed && !SDCard.volume.exists((std::string("/")+stageName).c_str()));return 0;}
  if(!empty){assert(state==Session::State::Body);
   assert(session->consume(reinterpret_cast<const uint8_t*>(input.data()+used),input.size()-used)==body.size());}
  assert(session->state()==Session::State::Complete);SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);std::array<char,4> actual{};
  assert(f && f.fileSize()==body.size());if(!empty)assert(f.read(actual.data(),4)==4 && std::memcmp(actual.data(),body.data(),4)==0);
  assert(f.close());auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("guarded_coalesced_stage=pass");return 0;
 }
 if(test.rfind("owner-stage",0)==0) {
  SDCard.device=&device;bool active=true;std::atomic_flag cancelled{};
  auto alive=[](void* p){return *static_cast<bool*>(p);};using Owner=wirecheck::ExFatStageOwner;
  auto owner=std::make_unique<Owner>(storage,Owner::Callbacks{&active,alive},cancelled);
  if(!exfat){assert(!owner->begin());std::puts("owner_rejects_non_exfat=pass");return 0;}
  assert(owner->begin());
  if(test=="owner-stage-cancel")cancelled.test_and_set();
  auto state=owner->step();unsigned steps=0;
  while(state==Owner::State::Partition || state==Owner::State::Boot || state==Owner::State::Layout || state==Owner::State::Scanning){
   assert(++steps<10000);state=owner->step();}
  if(test=="owner-stage-bad-boot" || test=="owner-stage-cancel") {
   assert(state==Owner::State::Failed && !SDCard.volume.exists((std::string("/")+stageName).c_str()));
   std::puts("owner_invalid_input_stops=pass");return 0;
  }
  assert(state==Owner::State::Ready);
  if(test=="owner-stage-terminal") {active=false;assert(!owner->beginStage(stageName,4,0));active=true;
   assert(!owner->beginStage(stageName,4,0) && !SDCard.volume.exists((std::string("/")+stageName).c_str()));
   std::puts("owner_failure_is_terminal=pass");return 0;}
  if(test=="owner-stage-stale") {auto f=storage.open("/Books/owner-change.tmp",O_CREAT|O_WRONLY);
   const uint8_t b=1;assert(f && f.write(&b,1)==1 && f.close());}
  const uint64_t reserve=test=="owner-stage-insufficient"?512U*1024*1024:64U*1024*1024;
  const bool begun=owner->beginStage(stageName,4,reserve);
  if(test=="owner-stage") {const uint8_t bytes[]={1,2,3,4};assert(begun && owner->append(bytes,4) && owner->finish());
   SDCard.volume.end();assert(SDCard.volume.begin(&device));auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
   uint8_t actual[4]{};assert(f && f.read(actual,4)==4 && std::memcmp(bytes,actual,4)==0 && f.close());
  } else {assert(!begun && !SDCard.volume.exists((std::string("/")+stageName).c_str()));}
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("owner_staging=pass");return 0;
 }
 if(test=="root-cursor") {
  std::vector<std::array<uint8_t,32>> expected;auto dir=SDCard.volume.open("/",O_RDONLY);assert(dir && dir.isDirectory());
  std::array<uint8_t,32> record{};while(true){const int n=dir.read(record.data(),32);assert(n==0 || n==32);
   if(!n || !record[0])break;expected.push_back(record);}assert(dir.close());
  HalStorage::RootDirectoryCursor cursor;size_t count=0;
  while(true){auto result=storage.readRootDirectoryRecord(snapshot,cursor,record.data());
   assert(result==HalStorage::SnapshotRead::Record || result==HalStorage::SnapshotRead::End);
   if(result==HalStorage::SnapshotRead::End || !record[0])break;
   assert(count<expected.size() && record==expected[count++]);}
  assert(count==expected.size() && count>100);std::puts("root_cursor_records_match=pass");return 0;
 }
 if(test=="root-cursor-failed-writer") {
  HalStorage::RootDirectoryCursor cursor;uint8_t record[32];
  assert(storage.readRootDirectoryRecord(snapshot,cursor,record)==HalStorage::SnapshotRead::Record);
  auto writer=storage.open("/Books/ROOTFAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t byte=0x47;assert(writer.write(&byte,1)==1);
  disk.failWriteAt=disk.writes;assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;
  const int before=disk.writes;assert(storage.readRootDirectoryRecord(snapshot,cursor,record)==HalStorage::SnapshotRead::Changed);
  assert(disk.writes==before);assert(writer.close());std::puts("root_cursor_does_not_retry_write=pass");return 0;
 }
 if(test=="stage-root-failed-writer") {
  assert(exfat);auto root=storage.open("/",O_RDONLY);assert(root && root.isDirectory());
  auto writer=storage.open("/Books/ROOTFAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t byte=0x47;assert(writer.write(&byte,1)==1);
  disk.failWriteAt=disk.writes;assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;
  assert(!storage.mutationSnapshotUnchanged(snapshot));const int before=disk.writes;
  const bool closed=root.close();const int writes=disk.writes-before;
  std::printf("root_close_after_failed_writer=%d writes=%d\n",closed,writes);
  assert(writes>0);assert(writer.close());return 0;
 }
 if(test=="volume-geometry") {
  SDCard.device=&device;
  Card::VolumeGeometry geometry{};assert(SDCard.volumeGeometry(geometry));
  assert(geometry.type==SDCard.volume.fatType() && geometry.deviceSectors==device.sectorCount() &&
   geometry.fatStart==SDCard.volume.fatStartSector() && geometry.dataStart==SDCard.volume.dataStartSector() &&
   geometry.clusterCount==SDCard.volume.clusterCount() && geometry.clusterBytes==SDCard.volume.bytesPerCluster());
  HalStorage::VolumeGeometry hal{};assert(storage.snapshotGeometry(snapshot,hal));
  assert(hal.type==geometry.type && hal.deviceSectors==geometry.deviceSectors && hal.fatStart==geometry.fatStart &&
   hal.dataStart==geometry.dataStart && hal.clusterCount==geometry.clusterCount && hal.clusterBytes==geometry.clusterBytes);
  SDCard.initialized=false;assert(!SDCard.volumeGeometry(geometry) && geometry.type==0 && geometry.deviceSectors==0);
  assert(!storage.snapshotGeometry(snapshot,hal) && hal.type==0);
  SDCard.initialized=true;auto savedDevice=SDCard.device;SDCard.device=nullptr;
  assert(!SDCard.volumeGeometry(geometry) && geometry.type==0);SDCard.device=savedDevice;
  auto f=storage.open("/Books/geometry-change.tmp",O_CREAT|O_WRONLY);const uint8_t byte=1;
  assert(f && f.write(&byte,1)==1 && f.close());assert(!storage.snapshotGeometry(snapshot,hal) && hal.type==0);
  std::puts("mounted_volume_geometry=pass");return 0;
 }
 if(test.rfind("exfat-boot-",0)==0) {
  assert(exfat);std::array<uint8_t,512> data{};assert(device.readSector(0,data.data()));
  const auto start=uint32_t(data[454])|(uint32_t(data[455])<<8)|(uint32_t(data[456])<<16)|(uint32_t(data[457])<<24);
  const auto partSize=uint32_t(data[458])|(uint32_t(data[459])<<8)|(uint32_t(data[460])<<16)|(uint32_t(data[461])<<24);
  wirecheck::ExFatBootCheck boot;assert(boot.begin({start,partSize,device.sectorCount(),SDCard.volume.fatStartSector(),
   SDCard.volume.dataStartSector(),SDCard.volume.clusterCount(),SDCard.volume.bytesPerCluster()}));
  bool ok=true;for(uint32_t n=0;n<12 && ok;++n){assert(storage.mutationSnapshotUnchanged(snapshot));
   assert(device.readSector(start+n,data.data()));ok=boot.consume(data.data(),data.size());}
  assert(storage.mutationSnapshotUnchanged(snapshot));
  if(test=="exfat-boot-valid")assert(ok && boot.state()==wirecheck::ExFatBootCheck::State::Complete);
  else {assert(!ok && boot.state()==wirecheck::ExFatBootCheck::State::Failed);
   if(test=="exfat-boot-two-fats")assert(SDCard.volume.fatCount()==1);}
  std::puts("exfat_boot_region_check=pass");return 0;
 }
 if(test.rfind("exfat-bitmap-layout",0)==0) {
  assert(exfat);SDCard.device=&device;
  struct BitmapContext {freeink::SdmmcBlockDevice* device;std::array<uint8_t,512> fat{};bool active=true;bool badMbr=false;
   HalStorage* storage=nullptr;HalStorage::MutationSnapshot* snapshot=nullptr;uint32_t bitmapStart=0;};
  BitmapContext context{&device};context.storage=&storage;context.snapshot=&snapshot;context.bitmapStart=SDCard.volume.dataStartSector();
  auto active=[](void* p){return static_cast<BitmapContext*>(p)->active;};
  auto readFat=[](void* p,uint32_t cluster,uint32_t& value){auto& c=*static_cast<BitmapContext*>(p);
   if(!c.storage->readExFatSector(*c.snapshot,HalStorage::ExFatSector::Fat,cluster/128,c.fat.data()))return false;
   const auto i=(cluster%128)*4;value=uint32_t(c.fat[i])|(uint32_t(c.fat[i+1])<<8)|
    (uint32_t(c.fat[i+2])<<16)|(uint32_t(c.fat[i+3])<<24);return true;};
  if(test.rfind("exfat-bitmap-layout-stage",0)==0) {
   SDCard.device=&device;HalStorage::VolumeGeometry mounted{};
   assert(storage.snapshotGeometry(snapshot,mounted) && mounted.type==FAT_TYPE_EXFAT);
   auto readBitmap=[](void* p,uint32_t sector,uint8_t* data){auto& c=*static_cast<BitmapContext*>(p);
    return sector>=c.bitmapStart && c.storage->readExFatSector(*c.snapshot,HalStorage::ExFatSector::Bitmap,sector-c.bitmapStart,data);};
   auto readBoot=[](void* p,uint32_t sector,uint8_t* data){auto& c=*static_cast<BitmapContext*>(p);
    if(!c.storage->readExFatSector(*c.snapshot,HalStorage::ExFatSector::Metadata,sector,data))return false;
    if(!sector && c.badMbr)data[454]^=1;return true;};
   assert(device.readSector(0,context.fat.data()));const auto start=uint32_t(context.fat[454])|
    (uint32_t(context.fat[455])<<8)|(uint32_t(context.fat[456])<<16)|(uint32_t(context.fat[457])<<24);
   const auto partSize=uint32_t(context.fat[458])|(uint32_t(context.fat[459])<<8)|
    (uint32_t(context.fat[460])<<16)|(uint32_t(context.fat[461])<<24);
   context.badMbr=test=="exfat-bitmap-layout-stage-bad-mbr";
   std::atomic_flag cancelled{};using Stage=wirecheck::ExFatReservedStage;using Counter=wirecheck::FreeClusterCounter;
   if(test=="exfat-bitmap-layout-stage-failed-writer") {
    auto stage=std::make_unique<Stage>(storage,snapshot,wirecheck::ExFatLayoutScan::Callbacks{&context,active,readFat},
     wirecheck::FreeSpaceScan::Callbacks{&context,active,readBoot},
     wirecheck::FreeSpaceScan::Callbacks{&context,active,readBitmap},cancelled);
    assert(stage->begin({Counter::Format::ExFat,mounted.dataStart,mounted.clusterCount,mounted.clusterBytes,mounted.deviceSectors},
     {start,partSize,mounted.deviceSectors,mounted.fatStart,mounted.dataStart,mounted.clusterCount,mounted.clusterBytes}));
    auto state=stage->step();while(state==Stage::State::Partition || state==Stage::State::Boot)state=stage->step();
    assert(state==Stage::State::Layout && stage->step()==Stage::State::Layout);
    auto writer=storage.open("/Books/ROOTFAULT.TMP",O_CREAT|O_WRONLY);const uint8_t byte=0x47;
    assert(writer && writer.write(&byte,1)==1);disk.failWriteAt=disk.writes;
    assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;const int before=disk.writes;
    assert(stage->step()==Stage::State::Failed);stage.reset();assert(disk.writes==before && writer.close());
    std::puts("stage_destroy_does_not_retry_failed_write=pass");return 0;
   }
   Stage stage(storage,snapshot,{&context,active,readFat},{&context,active,readBoot},
               {&context,active,readBitmap},cancelled);
   assert(stage.begin({Counter::Format::ExFat,mounted.dataStart,mounted.clusterCount,
                       mounted.clusterBytes,mounted.deviceSectors},
                      {start,partSize,mounted.deviceSectors,mounted.fatStart,mounted.dataStart,
                       mounted.clusterCount,mounted.clusterBytes}));
   if(test=="exfat-bitmap-layout-stage-boot-fault")disk.failAt=disk.reads;
   if(test=="exfat-bitmap-layout-stage-boot-stale") {
    auto f=storage.open("/Books/boot-stale.tmp",O_CREAT|O_WRONLY);const uint8_t b=1;
    assert(f && f.write(&b,1)==1 && f.close());
   }
   auto state=stage.step();unsigned steps=0;
   while(state==Stage::State::Partition || state==Stage::State::Boot || state==Stage::State::Layout || state==Stage::State::Scanning){assert(++steps<10000);state=stage.step();}
   if(test=="exfat-bitmap-layout-stage-bad-layout" || test=="exfat-bitmap-layout-stage-bad-boot" ||
      test=="exfat-bitmap-layout-stage-boot-fault" || test=="exfat-bitmap-layout-stage-boot-stale" ||
      test=="exfat-bitmap-layout-stage-bad-mbr" || test=="exfat-bitmap-layout-stage-bitmap-free") {
    if(test=="exfat-bitmap-layout-stage-boot-fault")assert(disk.injected);disk.failAt=-1;
    assert(state==Stage::State::Failed && !stage.beginStage(stageName,4,0));
    assert(!SDCard.volume.exists((std::string("/")+stageName).c_str()));
    std::puts("invalid_layout_blocks_stage=pass");return 0;
   }
   assert(state==Stage::State::Ready);
   if(test=="exfat-bitmap-layout-stage-stale") {
    auto f=storage.open("/Books/stale.tmp",O_CREAT|O_WRONLY);const uint8_t b=1;
    assert(f && f.write(&b,1)==1 && f.close());
   }
   const uint64_t reserve=test=="exfat-bitmap-layout-stage-insufficient"?512U*1024*1024:64U*1024*1024;
   const bool begun=stage.beginStage(stageName,4,reserve);
   if(test=="exfat-bitmap-layout-stage") {
    const uint8_t bytes[]={1,2,3,4};assert(begun && stage.append(bytes,4) && stage.finish());
    SDCard.volume.end();assert(SDCard.volume.begin(&device));auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
    uint8_t readback[4]{};assert(f && f.read(readback,4)==4 && std::memcmp(bytes,readback,4)==0 && f.close());
   } else {assert(!begun && stage.state()==Stage::State::Failed);
    assert(!SDCard.volume.exists((std::string("/")+stageName).c_str()));}
   auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5]{};
   assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
   std::puts("validated_exfat_reserved_stage=pass");return 0;
  }
  wirecheck::ExFatLayoutScan scan(storage,snapshot,{&context,active,readFat});
  assert(scan.begin(SDCard.volume.clusterCount(),SDCard.volume.bytesPerCluster(),1));
  if(test=="exfat-bitmap-layout-read-fault")disk.failAt=disk.reads;
  if(test=="exfat-bitmap-layout-cancel")context.active=false;
  if(test=="exfat-bitmap-layout-mutation") {
   auto f=storage.open("/Books/layout-mutation.tmp",O_CREAT|O_WRONLY);
   const uint8_t byte='x';assert(f && f.write(&byte,1)==1 && f.close());
  }
  auto state=scan.step();unsigned steps=0;
  while(state==wirecheck::ExFatLayoutScan::State::Records || state==wirecheck::ExFatLayoutScan::State::Chain){
   assert(++steps<10000);state=scan.step();}
  const bool valid=scan.valid();if(test=="exfat-bitmap-layout-read-fault")assert(disk.injected);
  disk.failAt=-1;
  if(test!="exfat-bitmap-layout") {assert(!valid && state==wirecheck::ExFatLayoutScan::State::Failed);
   std::puts("real_exfat_bitmap_corruption_rejected=pass");return 0;}
  assert(valid && state==wirecheck::ExFatLayoutScan::State::Complete);
  std::puts("real_exfat_bitmap_layout=pass");return 0;
 }
#ifdef EXPERIMENT_CACHE_READ
 if(test=="exfat-fat-cache") {
  assert(exfat);SDCard.device=&device;std::array<uint8_t,512> raw{},cached{},other{};
  const auto start=SDCard.volume.fatStartSector();assert(device.readSector(start,raw.data()));
  const int writes=disk.writes;assert(SDCard.volume.experimentalFlipFatByte(0,8));
  assert(storage.readExFatSector(snapshot,HalStorage::ExFatSector::Fat,0,cached.data()) && cached[8]==(raw[8]^1));
  assert(device.readSector(start,other.data()) && other==raw && disk.writes==writes);
  disk.failWriteAt=writes;assert(storage.readExFatSector(snapshot,HalStorage::ExFatSector::Fat,1,other.data()));
  assert(!disk.writeInjected && disk.writes==writes);disk.failWriteAt=-1;
  const int reads=disk.reads;assert(!storage.readExFatSector(snapshot,HalStorage::ExFatSector::Fat,UINT32_MAX,other.data()));
  assert(disk.reads==reads && disk.writes==writes);
  std::puts("exfat_fat_cache_safe_read=pass");return 0;
 }
 if(test=="allocation-cache-coherence") {
  SDCard.device=&device;
  std::array<uint8_t,512> raw{},cached{},changed{};
  const auto start=exfat?SDCard.volume.dataStartSector():SDCard.volume.fatStartSector();
  assert(device.readSector(start,raw.data()));const int before=disk.writes;
  auto readAllocation=[&](uint32_t offset,uint8_t* out){return exfat?
   storage.readExFatSector(snapshot,HalStorage::ExFatSector::Bitmap,offset,out):SDCard.volume.experimentalReadAllocation(offset,out);};
  assert(readAllocation(0,cached.data()) && cached==raw);
  assert(SDCard.volume.experimentalFlipAllocationByte(0,7));
  assert(readAllocation(0,changed.data()) && changed[7]==(raw[7]^1));
  assert(device.readSector(start,cached.data()) && cached==raw && disk.writes==before);
  const int reads=disk.reads;assert(!readAllocation(UINT32_MAX,cached.data()));
  assert(disk.reads==reads && disk.writes==before);
  std::puts("cached_allocation_observes_unflushed_change=pass");return 0;
 }
 if(test=="allocation-cache-eviction-fault") {
  SDCard.device=&device;
  std::array<uint8_t,512> bytes{};assert(SDCard.volume.experimentalFlipAllocationByte(0,7));
  const int before=disk.writes;disk.failWriteAt=before;
  const bool ok=exfat?storage.readExFatSector(snapshot,HalStorage::ExFatSector::Bitmap,1,bytes.data()):
   SDCard.volume.experimentalReadAllocation(1,bytes.data());
  const bool injected=disk.writeInjected;disk.failWriteAt=-1;
  assert(ok && !injected && disk.writes==before);
  std::puts("allocation_cache_safe_read_avoids_eviction_write=pass");return 0;
 }
 if(test=="allocation-cache-read-fault") {
  SDCard.device=&device;
  std::array<uint8_t,512> before{},after{};assert(SDCard.volume.experimentalFlipAllocationByte(0,7));
  assert(exfat?storage.readExFatSector(snapshot,HalStorage::ExFatSector::Bitmap,0,before.data()):
   SDCard.volume.experimentalReadAllocation(0,before.data()));const int writes=disk.writes;
  disk.failAt=disk.reads;const bool ok=exfat?storage.readExFatSector(snapshot,HalStorage::ExFatSector::Bitmap,1,after.data()):
   SDCard.volume.experimentalReadAllocation(1,after.data());assert(!ok && disk.injected);
  if(exfat)assert(!storage.mutationSnapshotUnchanged(snapshot));
  disk.failAt=-1;assert(SDCard.volume.experimentalReadAllocation(0,after.data()) && after==before);
  assert(disk.writes==writes);std::puts("allocation_read_fault_keeps_dirty_cache=pass");return 0;
 }
#endif
 if(test=="hash-position-cursor" || test=="hash-position-seek") {
  const auto cluster=SDCard.volume.bytesPerCluster();const uint64_t size=uint64_t(cluster)*12-137;
  fspos_t cursor{};std::array<uint8_t,512> bytes{};uint64_t position=0;disk.reads=0;
  while(position<size) {
   assert(storage.mutationSnapshotUnchanged(snapshot));
   auto reader=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
   assert(reader && reader.fileSize()==size);
   if(test=="hash-position-cursor")reader.fsetpos(&cursor);else assert(reader.seekSet(position));
   const auto count=static_cast<size_t>(std::min<uint64_t>(bytes.size(),size-position));
   assert(reader.read(bytes.data(),count)==static_cast<int>(count));
   for(size_t i=0;i<count;++i){const auto absolute=position+i;const auto block=absolute/cluster,offset=absolute%cluster;
    assert(bytes[i]==static_cast<uint8_t>((block*37+offset*13+offset/251)%251));}
   reader.fgetpos(&cursor);position+=count;assert(cursor.position==position && reader.close());
   assert(storage.mutationSnapshotUnchanged(snapshot));
  }
  std::printf("mode=%s bytes=%llu sector_reads=%d\n",test.c_str(),static_cast<unsigned long long>(size),disk.reads);
  return 0;
 }
 if(test=="hash-start-with-writer") {
  auto writer=storage.open("/Books/ACTIVE.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t value=0x31;assert(writer.write(&value,1)==1);
  const int reads=disk.reads,writes=disk.writes;std::array<uint8_t,128> scratch{};
  wirecheck::StageHash hash(storage,scratch.data(),scratch.size());
  assert(!hash.begin(stageName,1024) && disk.reads==reads && disk.writes==writes);
  assert(writer.close());std::puts("hash_refuses_active_writer_without_io=pass");return 0;
 }
 if(test=="exfat-sync-fault-sweep" || test=="exfat-sync-read-sweep") {
  assert(exfat);const auto path=std::string("/Books/")+std::string(180,'q')+".epub";
  auto writer=storage.open(path.c_str(),O_CREAT|O_WRONLY);assert(writer);
  const uint8_t bytes[]={0x17,0x39,0x56};assert(writer.write(bytes,sizeof(bytes))==sizeof(bytes));
  if(test=="exfat-sync-read-sweep")disk.failAt=disk.reads+fault;else disk.failWriteAt=disk.writes+fault;
  const bool syncOk=writer.sync();const bool injected=disk.writeInjected || disk.injected;
  disk.failWriteAt=-1;disk.failAt=-1;const bool closed=writer.close();SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open(path.c_str(),O_RDONLY);uint8_t out[3]{};
  const auto size=saved?saved.fileSize():0;const int got=saved?saved.read(out,sizeof(out)):-1;
  const bool matches=got==3 && std::memcmp(out,bytes,3)==0;
  std::printf("fault=%d injected=%d sync=%d close=%d size=%llu bytes=%d matches=%d\n",fault,injected,
              syncOk,closed,static_cast<unsigned long long>(size),got,matches);
  if(saved)assert(saved.close());
  assert(closed && size==sizeof(bytes) && matches);
  assert(storage.lookupPath(path.c_str())==HalStorage::PathStatus::Present);
  auto progress=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char state[5]{};
  assert(progress && progress.read(state,5)==5 && std::memcmp(state,"state",5)==0 && progress.close());return 0;
 }
 if(test=="exfat-close-first-failure" || test=="exfat-persistent-close-failure") {
  assert(exfat);auto writer=storage.open("/Books/CLOSE.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t data[]={0x17,0x39,0x56};assert(writer.write(data,sizeof(data))==sizeof(data));
  if(test=="exfat-persistent-close-failure") {
   disk.failWriteAt=disk.writes;assert(!writer.sync() && disk.writeInjected);
  }
  disk.writeInjected=false;disk.failWriteAt=disk.writes;const bool closed=writer.close();
  const bool injected=disk.writeInjected;disk.failWriteAt=-1;
  HalStorage::MutationSnapshot next;assert(injected && !closed && !storage.beginMutationSnapshot(next));
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open("/Books/CLOSE.TMP",O_RDONLY);
  assert(saved);const auto size=saved.fileSize();assert(saved.close());
  auto progress=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char state[5]{};
  assert(progress && progress.read(state,5)==5 && std::memcmp(state,"state",5)==0 && progress.close());
  std::printf("close_failure_reported=1 snapshot_rejected=1 remounted_size=%llu\n",static_cast<unsigned long long>(size));
  return 0;
 }
#ifdef EXPERIMENT_READ_DISCARD
 if(test=="hash-read-only-clean") {
  const int before=disk.writes;auto reader=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
  assert(reader && reader.isFile() && !reader.isWritable() && !reader.experimentalIsDirtyForTest());
  assert(reader.fileSize()==1024);std::array<uint8_t,127> buffer{};size_t total=0;
  for(;;){const int count=reader.read(buffer.data(),buffer.size());assert(count>=0);
   if(!count)break;total+=count;assert(!reader.experimentalIsDirtyForTest());}
  assert(total==1024 && reader.experimentalDiscardReadOnly()==1 && !reader.isOpen());
  assert(disk.writes==before);std::puts("hash_read_only_operations_stay_clean=pass");return 0;
 }
 if(test=="read-discard-writer-control") {
  auto writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t value=0x47;assert(writer.write(&value,1)==1);disk.failWriteAt=disk.writes;
  const bool firstSync=writer.sync();const bool injected=disk.writeInjected;disk.failWriteAt=-1;
  const bool closed=writer.close();SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open("/Books/FAULT.TMP",O_RDONLY);uint8_t out=0;
  const auto size=saved?saved.fileSize():0;const int got=saved?saved.read(&out,1):-1;
  std::printf("control sync=%d injected=%d closed=%d open=%d size=%llu bytes=%d value=%u expected=%u\n",
              firstSync,injected,closed,static_cast<bool>(saved),static_cast<unsigned long long>(size),got,out,value);
  if(saved)assert(saved.close());return 0;
 }
 if(test=="exfat-close-retry-prototype") {
  assert(exfat);auto writer=SDCard.volume.open("/Books/CLOSE.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t bytes[]={0x17,0x39,0x56};assert(writer.write(bytes,sizeof(bytes))==sizeof(bytes));
  disk.failWriteAt=disk.writes;assert(!writer.close() && disk.writeInjected && writer.isOpen());
  disk.writeInjected=false;disk.failWriteAt=disk.writes;
  assert(!writer.open(&SDCard.volume,"/Books/OTHER.TMP",O_CREAT|O_WRONLY) && disk.writeInjected && writer.isOpen());
  disk.failWriteAt=-1;assert(writer.close() && !writer.isOpen());
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open("/Books/CLOSE.TMP",O_RDONLY);uint8_t actual[3]{};
  assert(saved && saved.read(actual,sizeof(actual))==3 && std::memcmp(actual,bytes,3)==0 && saved.close());
  assert(!SDCard.volume.exists("/Books/OTHER.TMP"));
  auto progress=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char state[5]{};
  assert(progress && progress.read(state,5)==5 && std::memcmp(state,"state",5)==0 && progress.close());
  std::puts("close_retry_preserves_handle_and_data=pass");return 0;
 }
 if(test=="exfat-hal-close-retry-prototype") {
  assert(exfat);auto writer=storage.open("/Books/CLOSE.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t bytes[]={0x17,0x39,0x56};assert(writer.write(bytes,sizeof(bytes))==sizeof(bytes));
  disk.failWriteAt=disk.writes;assert(!writer.close() && disk.writeInjected && writer);
  HalStorage::MutationSnapshot afterFailure;assert(!storage.beginMutationSnapshot(afterFailure));
  disk.failWriteAt=-1;assert(writer.close() && !writer);
  HalStorage::MutationSnapshot afterRetry;assert(!storage.beginMutationSnapshot(afterRetry));
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open("/Books/CLOSE.TMP",O_RDONLY);uint8_t actual[3]{};
  assert(saved && saved.read(actual,sizeof(actual))==3 && std::memcmp(actual,bytes,3)==0 && saved.close());
  std::puts("hal_close_retry_preserves_data_and_poison=pass");return 0;
 }
 if(test=="exfat-close-retry-move-hazard") {
  assert(exfat);auto writer=SDCard.volume.open("/Books/CLOSE.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t byte=0x47;assert(writer.write(&byte,1)==1);disk.failWriteAt=disk.writes;
  assert(!writer.close() && disk.writeInjected && writer.isOpen());disk.failWriteAt=-1;
  FsFile empty;writer=std::move(empty);assert(!writer);
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open("/Books/CLOSE.TMP",O_RDONLY);const auto size=saved?saved.fileSize():0;
  std::printf("move_after_failed_close_target_size=%llu\n",static_cast<unsigned long long>(size));
  if(saved)assert(saved.close());return 0;
 }
 if(test=="hash-directory-reject") {
  HalFile writer;int before=0;
  {
   std::array<uint8_t,128> scratch{};wirecheck::StageHash hash(storage,scratch.data(),scratch.size());
   assert(!hash.begin(stageName,0));
   writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
   const uint8_t value=0x47;assert(writer.write(&value,1)==1);
   disk.failWriteAt=disk.writes;assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;before=disk.writes;
  }
  const int writes=disk.writes-before;std::printf("rejected_directory_destroy_writes=%d\n",writes);
  assert(writes==0 && writer.close());return 0;
 }
 if(test=="read-discard-failed-writer") {
  HalFile writer;int before=0;uint8_t value=0;
  {
   auto reader=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);assert(reader);
   assert(reader.read(&value,1)==1);
   writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
   assert(writer.write(&value,1)==1);disk.failWriteAt=disk.writes;
   assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;before=disk.writes;
   assert(!writer.experimentalDiscardReadOnly() && writer.impl->file.isOpen());
   assert(reader.experimentalDiscardReadOnly() && !reader.isOpen());
  }
  assert(disk.writes==before);assert(writer.close());
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto saved=SDCard.volume.open("/Books/FAULT.TMP",O_RDONLY);uint8_t out=0xff;
  const auto savedSize=saved?saved.fileSize():0;const int got=saved?saved.read(&out,1):-1;
  std::printf("writer_readback_open=%d size=%llu bytes=%d value=%u expected=%u\n",static_cast<bool>(saved),
              static_cast<unsigned long long>(savedSize),got,out,value);
  assert(saved && got==1 && out==value && saved.close());
  auto progress=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char stateBytes[5]{};
  assert(progress && progress.read(stateBytes,5)==5 && std::memcmp(stateBytes,"state",5)==0 && progress.close());
  std::puts("read_discard_does_not_flush_failed_writer=pass");return 0;
 }
 if(test=="read-discard-guards") {
  FsFile empty;assert(!empty.experimentalDiscardReadOnly());
  auto directory=SDCard.volume.open("/Books",O_RDONLY);assert(directory && directory.isDirectory());
  assert(directory.experimentalDiscardReadOnly()==1 && !directory.isOpen());
  auto source=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);assert(source);
  FsFile moved(std::move(source));assert(!source.experimentalDiscardReadOnly() && moved.isOpen());
  const int before=disk.writes;assert(moved.experimentalDiscardReadOnly());
  assert(!moved.experimentalDiscardReadOnly() && !moved.isOpen() && disk.writes==before);
  auto dirty=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);assert(dirty);
  dirty.experimentalSetDirtyForTest();assert(dirty.experimentalDiscardReadOnly()==2 && !dirty.isOpen());
  std::puts("read_discard_guards_pass");return 0;
 }
 if(test=="read-discard-rejected-destructor") {
  HalFile writer;int before=0;
  {
   auto reader=storage.open((std::string("/")+stageName).c_str(),O_RDONLY);assert(reader);
   reader.impl->file.experimentalSetDirtyForTest();
   writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
   const uint8_t value=0x47;assert(writer.write(&value,1)==1);disk.failWriteAt=disk.writes;
   assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;before=disk.writes;
   assert(!reader.experimentalDiscardReadOnly());
  }
  const int destructorWrites=disk.writes-before;HalStorage::MutationSnapshot next;
  assert(destructorWrites==0 && !storage.beginMutationSnapshot(next) && writer.close());
  std::printf("dirty_discard_writes=%d snapshot_rejected=1\n",destructorWrites);return 0;
 }
 if(test=="read-discard-dirty-poison") {
  auto reader=storage.open((std::string("/")+stageName).c_str(),O_RDONLY);assert(reader);
  reader.impl->file.experimentalSetDirtyForTest();const int before=disk.writes;
  assert(!reader.experimentalDiscardReadOnly() && !reader);
  HalStorage::MutationSnapshot next;assert(!storage.beginMutationSnapshot(next) && disk.writes==before);
  std::puts("dirty_discard_alone_poisoned_snapshot_without_writes=pass");return 0;
 }
#endif
 if(test=="hash-failed-writer-close") {
  std::array<uint8_t,128> scratch{};wirecheck::StageHash hash(storage,scratch.data(),scratch.size());
  assert(hash.begin(stageName,1024));assert(hash.step()==wirecheck::StageHash::State::Reading);
  auto writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
  const uint8_t b=0x47;assert(writer.write(&b,1)==1);
  disk.failWriteAt=disk.writes;const bool syncOk=writer.sync();const bool injected=disk.writeInjected;
  disk.failWriteAt=-1;const int before=disk.writes;
  const auto state=hash.step();const int during=disk.writes-before;
  std::printf("sync_ok=%d injected=%d hash_failed=%d hash_close_writes=%d\n",syncOk,injected,
              state==wirecheck::StageHash::State::Failed,during);
  assert(injected && !syncOk && state==wirecheck::StageHash::State::Failed && during==0);
  assert(writer.close());return 0;
 }
#ifdef EXPERIMENT_READ_DISCARD
 if(test=="hash-unmounted") {
  int beforeReads=0,beforeWrites=0;
  {
   std::array<uint8_t,128> scratch{};wirecheck::StageHash hash(storage,scratch.data(),scratch.size());
   assert(hash.begin(stageName,1024) && hash.step()==wirecheck::StageHash::State::Reading);
   SDCard.mounted=false;beforeReads=disk.reads;beforeWrites=disk.writes;
   assert(hash.step()==wirecheck::StageHash::State::Failed);
  }
  assert(disk.reads==beforeReads && disk.writes==beforeWrites);
  std::puts("hash_unmounted_no_further_io=pass");return 0;
 }
 if(test=="hash-destroy-failed-writer") {
  HalFile writer;int before=0;
  {
   std::array<uint8_t,128> scratch{};wirecheck::StageHash hash(storage,scratch.data(),scratch.size());
   assert(hash.begin(stageName,1024));assert(hash.step()==wirecheck::StageHash::State::Reading);
   writer=storage.open("/Books/FAULT.TMP",O_CREAT|O_WRONLY);assert(writer);
   const uint8_t value=0x47;assert(writer.write(&value,1)==1);
   disk.failWriteAt=disk.writes;assert(!writer.sync() && disk.writeInjected);disk.failWriteAt=-1;before=disk.writes;
  }
  std::printf("hash_destroy_writes=%d\n",disk.writes-before);
  assert(disk.writes==before && writer.close());return 0;
 }
#endif
 if(test=="hash-fragmented") {
  const auto cluster=SDCard.volume.bytesPerCluster();const uint64_t size=uint64_t(cluster)*12-137;
  std::array<uint8_t,512> buffer{};wirecheck::StageHash hash(storage,buffer.data(),buffer.size());
  disk.reads=0;assert(hash.begin(stageName,size));unsigned steps=0;auto state=wirecheck::StageHash::State::Reading;
  while(state==wirecheck::StageHash::State::Reading){state=hash.step();++steps;}
  uint8_t digest[32]{};assert(state==wirecheck::StageHash::State::Complete && hash.copyDigest(digest));
  assert(steps==(size+buffer.size()-1)/buffer.size());
  assert(hashObserved.size()==size);uint8_t sum=0;
  for(uint64_t i=0;i<size;++i){const auto block=i/cluster,offset=i%cluster;
   const auto expected=static_cast<uint8_t>((block*37+offset*13+offset/251)%251);
   assert(hashObserved[i]==expected);sum+=expected;}
  for(uint8_t byte:digest)assert(byte==sum);
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char stateBytes[5]{};
  assert(saved && saved.read(stateBytes,5)==5 && std::memcmp(stateBytes,"state",5)==0 && saved.close());
  std::printf("fragmented_stage_hash_pass steps=%u sector_reads=%d\n",steps,disk.reads);return 0;
 }
 if(test=="alias-collision-stress") {
  assert(!exfat);
  for(int i=0;i<99;++i) {
   const auto path=std::string("/Books/collision-book-")+std::to_string(i)+".epub";
   auto f=storage.open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY);
   if(!f)std::fprintf(stderr,"collision create failed at %d\n",i);
   assert(f);const uint8_t value=static_cast<uint8_t>(i);
   assert(f.write(&value,1)==1 && f.close());
  }
  auto extra=storage.open("/Books/collision-book-99.epub",O_CREAT|O_EXCL|O_WRONLY);
  assert(!extra);
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  for(int i=0;i<99;++i) {
   const auto path=std::string("/Books/collision-book-")+std::to_string(i)+".epub";
   auto f=SDCard.volume.open(path.c_str(),O_RDONLY);uint8_t value=255;
   assert(f && f.read(&value,1)==1 && value==static_cast<uint8_t>(i) && f.close());
  }
  assert(!SDCard.volume.exists("/Books/collision-book-99.epub"));
  auto progress=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char state[5]{};
  assert(progress && progress.read(state,5)==5 && std::memcmp(state,"state",5)==0 && progress.close());
  std::puts("alias_exhaustion_preserves_existing_files=pass");return 0;
 }
 if(test=="space-open-writer") {
  auto f=storage.open("/Books/open-writer.tmp",O_CREAT|O_WRONLY);assert(f);
  const uint8_t value=0x42;assert(f.write(&value,1)==1);
  HalStorage::MutationSnapshot during;assert(!storage.beginMutationSnapshot(during));
  assert(!storage.mutationSnapshotUnchanged(snapshot));
  assert(f.close());HalStorage::MutationSnapshot after;assert(storage.beginMutationSnapshot(after));
  assert(storage.mutationSnapshotUnchanged(after));
  std::puts("open_writer_blocks_snapshot=pass");return 0;
 }
 if(test=="space-closed-writer-cache" || test=="space-closed-writer-cache-long" ||
    test=="space-closed-writer-cache-long-nodelete") {
  using Counter=wirecheck::FreeClusterCounter;
  const auto format=exfat?Counter::Format::ExFat:SDCard.volume.fatType()==16?Counter::Format::Fat16:Counter::Format::Fat32;
  for(int i=0;i<12;++i) {
   const auto path=test!="space-closed-writer-cache"
       ? std::string("/Books/cache-test-")+std::to_string(i)+".tmp"
       : std::string("/Books/T")+std::to_string(i)+".TMP";
   auto f=storage.open(path.c_str(),O_CREAT|O_WRONLY);
   if(!f)std::fprintf(stderr,"create failed at iteration %d\n",i);assert(f);
   std::array<uint8_t,1024> payload{};assert(f.write(payload.data(),payload.size())==payload.size() && f.close());
   if(i%2==0 && test!="space-closed-writer-cache-long-nodelete")assert(storage.remove(path.c_str()));
  }
  {
   HalStorage::MutationSnapshot fresh;assert(storage.beginMutationSnapshot(fresh));
   Counter counter;assert(counter.begin(format,SDCard.volume.clusterCount(),SDCard.volume.bytesPerCluster()));
   auto sector=exfat?SDCard.volume.dataStartSector():SDCard.volume.fatStartSector();
   std::array<uint8_t,512> data{};auto state=Counter::State::Reading;
   while(state==Counter::State::Reading){assert(device.readSector(sector++,data.data()));state=counter.consume(data.data(),data.size());}
   uint64_t raw=0;assert(state==Counter::State::Complete && counter.freeBytes(raw));
   assert(storage.mutationSnapshotUnchanged(fresh));
   SDCard.volume.end();assert(SDCard.volume.begin(&device));
   const auto afterRemount=SDCard.volume.freeClusterCount();assert(afterRemount>=0);
   assert(raw==uint64_t(afterRemount)*SDCard.volume.bytesPerCluster());
   for(int i=0;i<12;++i) {
    const auto path=test!="space-closed-writer-cache"
        ? std::string("/Books/cache-test-")+std::to_string(i)+".tmp"
        : std::string("/Books/T")+std::to_string(i)+".TMP";
    const bool deleted=i%2==0 && test!="space-closed-writer-cache-long-nodelete";
    assert(SDCard.volume.exists(path.c_str())!=deleted);
    if(!deleted) {auto saved=SDCard.volume.open(path.c_str(),O_RDONLY);std::array<uint8_t,1024> bytes{};
     assert(saved && saved.read(bytes.data(),bytes.size())==static_cast<int>(bytes.size()) && saved.close());}
   }
   auto progress=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char savedState[5]{};
   assert(progress && progress.read(savedState,5)==5 && std::memcmp(savedState,"state",5)==0 && progress.close());
  }
  std::puts("closed_writer_raw_allocation_matches_remount=pass");return 0;
 }
 if(test=="reserved-stage" || test=="reserved-insufficient" || test=="reserved-stale") {
  struct Context{freeink::SdmmcBlockDevice* device;};Context context{&device};
  auto active=[](void*){return true;};
  auto read=[](void* p,uint32_t sector,uint8_t* data){return static_cast<Context*>(p)->device->readSector(sector,data);};
  using Counter=wirecheck::FreeClusterCounter;using Stage=wirecheck::ReservedStage;
  const auto type=SDCard.volume.fatType();assert(exfat || type==16 || type==32);
  const auto format=exfat?Counter::Format::ExFat:type==16?Counter::Format::Fat16:Counter::Format::Fat32;
  const auto start=exfat?SDCard.volume.dataStartSector():SDCard.volume.fatStartSector();
  std::atomic_flag cancelled{};Stage stage(storage,snapshot,{&context,active,read},cancelled);
  assert(stage.beginScan({format,start,SDCard.volume.clusterCount(),SDCard.volume.bytesPerCluster(),device.sectorCount()}));
  while(stage.stepScan()==Stage::State::Scanning){}assert(stage.state()==Stage::State::Ready);
  if(test=="reserved-stale") {
   auto f=storage.open("/Books/change.tmp",O_CREAT|O_WRONLY);assert(f);
   const uint8_t b=1;assert(f.write(&b,1)==1 && f.close());
  }
  const uint64_t reserve=test=="reserved-insufficient"?512U*1024*1024:64U*1024*1024;
  const bool begun=stage.beginStage(stageName,4,reserve);
  if(test=="reserved-stage") {
   assert(begun);const uint8_t data[]={1,2,3,4};assert(stage.append(data,4) && stage.finish());
   SDCard.volume.end();assert(SDCard.volume.begin(&device));
   auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);uint8_t out[4]{};
   assert(f && f.read(out,4)==4 && std::memcmp(data,out,4)==0 && f.close());
  } else {
   assert(!begun && stage.state()==Stage::State::Failed);
   assert(!SDCard.volume.exists((std::string("/")+stageName).c_str()));
  }
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5];
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("reserved_stage_admission=pass");return 0;
 }
 if(test=="space-scan" || test=="space-scan-fault" || test=="space-scan-change") {
  struct Context{freeink::SdmmcBlockDevice* device;bool active=true;};Context context{&device};
  auto active=[](void* p){return static_cast<Context*>(p)->active;};
  auto read=[](void* p,uint32_t sector,uint8_t* data){return static_cast<Context*>(p)->device->readSector(sector,data);};
  using Counter=wirecheck::FreeClusterCounter;using Scan=wirecheck::FreeSpaceScan;
  const auto type=SDCard.volume.fatType();assert(exfat || type==16 || type==32);
  const auto format=exfat?Counter::Format::ExFat:type==16?Counter::Format::Fat16:Counter::Format::Fat32;
  const auto start=exfat?SDCard.volume.dataStartSector():SDCard.volume.fatStartSector();
  Scan scan(storage,snapshot,{&context,active,read});
  assert(scan.begin({format,start,SDCard.volume.clusterCount(),SDCard.volume.bytesPerCluster(),device.sectorCount()}));
  if(test=="space-scan-fault"){disk.reads=0;disk.failAt=0;}
  auto result=scan.step();
  if(test=="space-scan-fault") {
   assert(disk.injected && result==Scan::State::Failed);disk.failAt=-1;
  } else if(test=="space-scan-change") {
   assert(result==Scan::State::Reading || result==Scan::State::Complete);
   auto f=storage.open("/Books/new.tmp",O_CREAT|O_WRONLY);assert(f);
   const uint8_t byte=4;assert(f.write(&byte,1)==1 && f.close());uint64_t ignored=0;
   if(result==Scan::State::Reading)assert(scan.step()==Scan::State::Failed);
   else assert(!scan.freeBytes(ignored) && !scan.allowsStage(0,0));
  } else {
   while(result==Scan::State::Reading)result=scan.step();
   assert(result==Scan::State::Complete);uint64_t counted=0;
   assert(scan.freeBytes(counted));const auto expected=SDCard.volume.freeClusterCount();
   assert(expected>=0 && counted==uint64_t(expected)*SDCard.volume.bytesPerCluster());
   assert(scan.allowsStage(1024,64U*1024*1024));
  }
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5];
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("guarded_free_space_scan=pass");return 0;
 }
 if(test=="space-count" || test=="space-fat32-high") {
  using Counter=wirecheck::FreeClusterCounter;Counter counter;
  const auto type=SDCard.volume.fatType();
  const auto format=exfat?Counter::Format::ExFat:type==16?Counter::Format::Fat16:Counter::Format::Fat32;
  assert(exfat || type==16 || type==32);
  const auto clusters=SDCard.volume.clusterCount();const auto clusterSize=SDCard.volume.bytesPerCluster();
  assert(counter.begin(format,clusters,clusterSize));
  auto sector=exfat?SDCard.volume.dataStartSector():SDCard.volume.fatStartSector();
  Counter::State result=Counter::State::Reading;std::array<uint8_t,512> allocation{};
  while(result==Counter::State::Reading){assert(device.readSector(sector++,allocation.data()));
   result=counter.consume(allocation.data(),allocation.size());}
  assert(result==Counter::State::Complete && storage.mutationSnapshotUnchanged(snapshot));
  const auto sdFree=SDCard.volume.freeClusterCount();assert(sdFree>=0);uint64_t counted=0;
  assert(counter.freeBytes(counted) && counted==uint64_t(sdFree)*clusterSize);
  if(test=="space-fat32-high") {
   assert(sdFree==fatFreeBefore-1);const uint64_t reserve=64U*1024*1024;
   assert(counted>reserve+2*clusterSize);const uint64_t limit=counted-reserve-2*clusterSize;
   assert(counter.allowsStage(limit,reserve) && !counter.allowsStage(limit+1,reserve));
  }
  assert(!counter.allowsStage(counted,0));std::printf("free_space_matches_sdfat=%llu\n",static_cast<unsigned long long>(counted));return 0;
 }
 if(test=="hash-success" || test=="hash-change" || test=="hash-after" || test=="hash-fault" || test=="hash-size") {
  std::array<uint8_t,128> scratch{};wirecheck::StageHash hash(storage,scratch.data(),scratch.size());uint8_t digest[32]{};
  if(test=="hash-size")assert(!hash.begin(stageName,1023));
  else {
   assert(hash.begin(stageName,1024));
   if(test=="hash-fault"){disk.reads=0;disk.failAt=0;}
   auto result=hash.step();
   if(test=="hash-fault") {
    assert(disk.injected && result==wirecheck::StageHash::State::Failed && !hash.copyDigest(digest));disk.failAt=-1;
   } else if(test=="hash-change") {
    assert(result==wirecheck::StageHash::State::Reading);
    const auto path=std::string("/")+stageName;auto old=SDCard.volume.open(path.c_str(),O_RDONLY);assert(old);
    auto slot=old.dirIndex();assert(old.close() && storage.remove(path.c_str()));
    const char* other="/__other_0123456789abcdef0123456789abcdef.part";
    auto replacement=storage.open(other,O_CREAT|O_EXCL|O_WRONLY);assert(replacement);
    const uint8_t value[]={8,9,10};assert(replacement.write(value,3)==3 && replacement.close());
    auto check=SDCard.volume.open(other,O_RDONLY);assert(check && check.dirIndex()==slot && check.close());
    assert(hash.step()==wirecheck::StageHash::State::Failed && !hash.copyDigest(digest));
    SDCard.volume.end();assert(SDCard.volume.begin(&device));check=SDCard.volume.open(other,O_RDONLY);uint8_t actual[3]{};
    assert(check && check.read(actual,3)==3 && std::memcmp(actual,value,3)==0 && check.close());
   } else {
    while(result==wirecheck::StageHash::State::Reading)result=hash.step();
    assert(result==wirecheck::StageHash::State::Complete);
    if(test=="hash-after") {
     const uint8_t bytes[]={8,9};auto changed=storage.open("/Books/unrelated.tmp",O_CREAT|O_WRONLY);
     assert(changed && changed.write(bytes,2)==2 && changed.close());
     assert(!hash.copyDigest(digest) && hash.state()==wirecheck::StageHash::State::Failed);
    } else {
     assert(hash.copyDigest(digest));uint8_t sum=0;for(int i=0;i<1024;++i)sum+=i%251;for(auto byte:digest)assert(byte==sum);
    }
   }
  }
  auto saved=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char progress[5];
  assert(saved && saved.read(progress,5)==5 && std::memcmp(progress,"state",5)==0 && saved.close());
  std::puts("stage_hash_storage_checks=pass");return 0;
 }
 if(test=="raw-stage" || test=="raw-short" || test=="raw-collision" || test=="raw-slot-reuse" || test=="raw-fault") {
  struct Callbacks {bool active=true;unsigned checks=0;};Callbacks cb;
  auto active=[](void* p){return static_cast<Callbacks*>(p)->active;};
  auto identity=[](void* p,const char* id){++static_cast<Callbacks*>(p)->checks;return std::string(id)==std::string(64,'a');};
  std::array<uint8_t,16> dataBuffer{};
  wirecheck::RawStageSession session(storage,dataBuffer.data(),dataBuffer.size(),{&cb,active,identity});
  const std::string header="PUT /api/sync/stage-raw HTTP/1.1\r\nContent-Length: 20\r\n"
    "Content-Type: application/octet-stream\r\nX-Xsync-Device: "+std::string(64,'a')+
    "\r\nX-Xsync-Stage: "+stageName+"\r\n\r\n";
  assert(session.consume(reinterpret_cast<const uint8_t*>(header.data()),header.size())==header.size());
  const std::string path=std::string("/")+stageName;
  if(test=="raw-collision") {
   assert(session.state()==wirecheck::RawStageSession::State::Failed);
   auto old=SDCard.volume.open(path.c_str(),O_RDONLY);char data[3];
   assert(old && old.read(data,3)==3 && std::memcmp(data,"old",3)==0 && old.close());
  } else {
   assert(session.state()==wirecheck::RawStageSession::State::Body);
   const std::string body="abcdefghijklmnopqrst";
   if(test=="raw-fault") {disk.writes=0;disk.failWriteAt=0;}
   const size_t count=test=="raw-stage"?body.size():16;
   assert(session.consume(reinterpret_cast<const uint8_t*>(body.data()),count)==count);
   if(test=="raw-stage")assert(session.state()==wirecheck::RawStageSession::State::Complete && cb.checks==2);
   else if(test=="raw-short"){session.cancel();assert(session.state()==wirecheck::RawStageSession::State::Failed);}
   else if(test=="raw-fault") {
    assert(disk.writeInjected && session.state()==wirecheck::RawStageSession::State::Failed);disk.failWriteAt=-1;
   }
   else {
    auto original=SDCard.volume.open(path.c_str(),O_RDONLY);assert(original);auto slot=original.dirIndex();assert(original.close());
    assert(storage.remove(path.c_str()));
    const char* replacement="/__other_0123456789abcdef0123456789abcdef.part";
    auto other=storage.open(replacement,O_CREAT|O_EXCL|O_WRONLY);assert(other);
    const uint8_t bytes[]={9,8,7,6,5};assert(other.write(bytes,5)==5 && other.close());
    auto check=SDCard.volume.open(replacement,O_RDONLY);assert(check && check.dirIndex()==slot && check.close());
    assert(session.consume(reinterpret_cast<const uint8_t*>(body.data()+16),4)==4);
    assert(session.state()==wirecheck::RawStageSession::State::Failed);
    SDCard.volume.end();assert(SDCard.volume.begin(&device));
    check=SDCard.volume.open(replacement,O_RDONLY);uint8_t actual[5]{};
    assert(check && check.read(actual,5)==5 && std::memcmp(actual,bytes,5)==0 && check.close());
   }
   if(test!="raw-slot-reuse" && test!="raw-fault") {
    SDCard.volume.end();assert(SDCard.volume.begin(&device));
    auto f=SDCard.volume.open(path.c_str(),O_RDONLY);char actual[20]{};
    assert(f && f.fileSize()==count && f.read(actual,count)==static_cast<int>(count));
    assert(std::memcmp(actual,body.data(),count)==0 && f.close());
   }
  }
#ifdef EXPERIMENT_WRITE_FENCE
  if(test=="raw-fault") {SDCard.volume.end();assert(SDCard.volume.begin(&device));}
#endif
  auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
  assert(state && state.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && state.close());
  auto book=SDCard.volume.open("/Books/5 unique book.epub",O_RDONLY);assert(book && book.read()==int('x') && book.close());
  std::puts("raw_session_existing_files_preserved=pass");return 0;
 }
 if(test=="section-page-short-write") {
  Section section;assert(section.openSpeculativeOutput());std::vector<uint8_t> header(HEADER_SIZE,0);
  assert(section.file.write(header.data(),header.size())==header.size() && section.file.sync());
  disk.writes=0;disk.failWriteAt=0;PageHorizontalRule rule;
  for(int i=0;i<1000 && !disk.writeInjected;++i) assert(rule.serialize(section.file));
  assert(disk.writeInjected && !storage.monitoredStorageHealthy());disk.failWriteAt=-1;
  const bool committed=storage.withStorageLock([](void* p){return static_cast<Section*>(p)->commitBuildFile(SECTION_FILE_VERSION,0,0);},&section);
  assert(!committed && !section.file);SDCard.volume.end();assert(SDCard.volume.begin(&device));
  assert(!SDCard.volume.exists(section.filePath.c_str()));
  if(SDCard.volume.exists(section.speculativeFilePath().c_str())){
   auto f=SDCard.volume.open(section.speculativeFilePath().c_str(),O_RDONLY);assert(f);const int marker=f.read();
   assert(marker==-1 || marker==0);assert(f.close());
  }
  auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
  assert(state && state.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && state.close());
  std::puts("section_page_short_write_unpublished=pass");return 0;
 }
 if(test=="section-direct-commit" || test=="section-direct-fault") {
  Section section;assert(section.openSpeculativeOutput());std::vector<uint8_t> header(HEADER_SIZE,0);
  assert(section.file.write(header.data(),header.size())==header.size());
  disk.writes=0;disk.failWriteAt=fault;
  const bool committed=storage.withStorageLock([](void* p){return static_cast<Section*>(p)->commitBuildFile(SECTION_FILE_VERSION,0,0);},&section);
  const bool injected=disk.writeInjected;disk.failWriteAt=-1;
  if(injected) assert(!committed);
  assert(!section.file);assert(!SDCard.volume.exists((section.filePath+".part").c_str()));
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  int observedMarker=-2;
  assert(!SDCard.volume.exists(section.filePath.c_str()));
  if(committed || SDCard.volume.exists(section.speculativeFilePath().c_str())) {
   auto f=SDCard.volume.open(section.speculativeFilePath().c_str(),O_RDONLY);assert(f);
   const int marker=f.read();observedMarker=marker;assert(marker==-1 || marker==0 || marker==SECTION_FILE_VERSION);
   if(marker==-1) assert(f.fileSize()==0);
   if(committed || marker==SECTION_FILE_VERSION) {
    assert(marker==SECTION_FILE_VERSION && f.fileSize()>=HEADER_SIZE+2*sizeof(uint16_t));
    assert(f.seekSet(HEADER_SIZE-sizeof(uint32_t)*5-sizeof(uint16_t)));
    uint8_t offsets[sizeof(uint16_t)+5*sizeof(uint32_t)]{};assert(f.read(offsets,sizeof(offsets))==sizeof(offsets));
    uint32_t lut=0,anchors=0;std::memcpy(&lut,offsets+sizeof(uint16_t),sizeof(lut));
    std::memcpy(&anchors,offsets+sizeof(uint16_t)+sizeof(uint32_t),sizeof(anchors));
    assert(lut==HEADER_SIZE && anchors==HEADER_SIZE);
   }
   assert(f.close());
  }
  if(injected && fault<2) assert(observedMarker!=SECTION_FILE_VERSION);
  auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
  assert(state && state.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && state.close());
  std::printf("section_direct_commit=%d injected=%d writes=%d marker=%d\n",committed,injected,disk.writes,observedMarker);return 0;
 }
 if(test=="section-direct-create") {
  Section section;const auto legacy=section.filePath+".part";
  auto opaque=storage.open(legacy.c_str(),O_CREAT|O_EXCL|O_WRONLY);assert(opaque);
  const uint8_t old[]={7,6,5};assert(opaque.write(old,3)==3 && opaque.close());
  assert(!section.openSpeculativeOutput());
  auto saved=SDCard.volume.open(legacy.c_str(),O_RDONLY);uint8_t actual[3]{};
  assert(saved && saved.read(actual,3)==3 && std::memcmp(actual,old,3)==0 && saved.close());
  assert(storage.remove(legacy.c_str()));assert(section.openSpeculativeOutput());
  const uint8_t data[]={0,1,2,3};assert(section.file.write(data,4)==4);
  assert(storage.withStorageLock([](void* p){return static_cast<Section*>(p)->closeSpeculativeOutput();},&section));
  assert(!section.file && !SDCard.volume.exists(legacy.c_str()));
  assert(storage.withStorageLock([](void* p){return static_cast<Section*>(p)->reopenSpeculativeOutput();},&section));
  assert(section.file.close());SDCard.volume.end();assert(SDCard.volume.begin(&device));
  assert(!SDCard.volume.exists(section.filePath.c_str()));
  auto output=SDCard.volume.open(section.speculativeFilePath().c_str(),O_RDONLY);uint8_t check[4]{};
  assert(output && output.read(check,4)==4 && std::memcmp(check,data,4)==0 && output.close());
  assert(!SDCard.volume.exists(legacy.c_str()));std::puts("section_direct_exclusive_preserved=pass");return 0;
 }
 if(test=="section-slot-reuse") {
  Section section;const std::string path=section.speculativeFilePath();
  section.file=storage.open(path.c_str(),O_CREAT|O_EXCL|O_RDWR);assert(section.file);
  const uint8_t initial[]={1,2,3,4};assert(section.file.write(initial,4)==4);
  assert(storage.withStorageLock([](void* p){return static_cast<Section*>(p)->closeSpeculativeOutput();},&section));
  assert(!section.file);auto original=SDCard.volume.open(path.c_str(),O_RDONLY);assert(original);
  auto slot=original.dirIndex();assert(original.close());assert(storage.remove(path.c_str()));
  const char* replacement="/__other_0123456789abcdef0123456789abcdef.part.prefetch";
  auto other=storage.open(replacement,O_CREAT|O_EXCL|O_WRONLY);assert(other);
  const uint8_t bytes[]={9,8,7,6,5};assert(other.write(bytes,5)==5 && other.close());
  auto check=SDCard.volume.open(replacement,O_RDONLY);assert(check && check.dirIndex()==slot && check.close());
  assert(!storage.withStorageLock([](void* p){return static_cast<Section*>(p)->reopenSpeculativeOutput();},&section));
  assert(!section.file);SDCard.volume.end();assert(SDCard.volume.begin(&device));
  check=SDCard.volume.open(replacement,O_RDONLY);uint8_t actual[5]{};
  assert(check && check.fileSize()==5 && check.read(actual,5)==5 && std::memcmp(actual,bytes,5)==0 && check.close());
  auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
  assert(state && state.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && state.close());
  std::puts("section_reused_slot_preserved=pass");return 0;
 }
 if(test=="stage" || test=="stage-collision" || test=="stage-fault" || test=="stage-invalid" ||
    test=="stage-short" || test=="stage-read-fault" || test=="stage-overrun" || test=="stage-empty" ||
    test=="stage-concurrent" || test=="stage-unrelated" || test=="stage-slot-reuse") {
  wirecheck::ExclusiveStage stage(storage);
  if(test=="stage-read-fault") {
   disk.reads=0;disk.failAt=0;assert(!stage.begin(stageName,4));assert(disk.injected);
   disk.failAt=-1;assert(!storage.monitoredStorageHealthy());
   assert(!SDCard.volume.exists((std::string("/")+stageName).c_str()));
   std::puts("stage_lookup_fault_rejected=pass");return 0;
  }
  if(test=="stage-invalid") {
   assert(!stage.begin("__xsync_0123456789abcdef0123456789abcdeF.part",4));
   assert(!stage.begin("../bad.epub",4));assert(!stage.begin(stageName,0x7fffffffULL));
   assert(storage.mutationSnapshotUnchanged(snapshot));std::puts("stage_invalid_rejected=pass");return 0;
  }
  if(test=="stage-collision") {
   assert(!stage.begin(stageName,4));assert(storage.mutationSnapshotUnchanged(snapshot));
   auto old=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);char bytes[3];
   assert(old && old.read(bytes,3)==3 && std::memcmp(bytes,"old",3)==0 && old.close());
   std::puts("stage_collision_preserved=pass");return 0;
  }
  assert(stage.begin(stageName,test=="stage-fault"?1024:test=="stage-empty"?0:4));
  assert(!storage.mutationSnapshotUnchanged(snapshot));
  if(test=="stage-slot-reuse") {
   const uint8_t data[]={1,2,3,4};assert(stage.append(data,4));
   const auto path=std::string("/")+stageName;
   auto original=SDCard.volume.open(path.c_str(),O_RDONLY);assert(original);
   const auto slot=original.dirIndex();assert(original.close());assert(storage.remove(path.c_str()));
   const char* replacement="/__other_0123456789abcdef0123456789abcdef.part";
   auto other=storage.open(replacement,O_CREAT|O_EXCL|O_WRONLY);assert(other);
   const uint8_t replacementData[]={9,8,7,6,5};assert(other.write(replacementData,5)==5 && other.close());
   auto check=SDCard.volume.open(replacement,O_RDONLY);assert(check && check.dirIndex()==slot && check.close());
   assert(!stage.finish());
   SDCard.volume.end();assert(SDCard.volume.begin(&device));
   check=SDCard.volume.open(replacement,O_RDONLY);uint8_t actual[5]{};
   assert(check && check.fileSize()==5 && check.read(actual,5)==5 && std::memcmp(actual,replacementData,5)==0 && check.close());
   auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
   assert(state && state.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && state.close());
   std::puts("reused_slot_preserved=pass");return 0;
  }
  if(test=="stage-concurrent" || test=="stage-unrelated") {
   const uint8_t data[]={1,2,3,4};assert(stage.append(data,4));
   const auto changed=test=="stage-concurrent"?(std::string("/")+stageName):"/Books/unrelated.tmp";
   auto other=storage.open(changed.c_str(),O_CREAT|O_WRONLY|O_TRUNC);assert(other);
   assert(other.write(data,1)==1 && other.close());assert(!stage.finish());
   assert(stage.state()==wirecheck::ExclusiveStage::State::Failed);
   if(test=="stage-concurrent") {auto current=SDCard.volume.open(changed.c_str(),O_RDONLY);
    assert(current && current.fileSize()==1 && current.read()==1 && current.close());}
   auto old=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
   assert(old && old.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && old.close());
   std::puts("intervening_mutation_rejected=pass");return 0;
  }
  if(test=="stage-empty") {assert(stage.finish());std::puts("empty_stage_closed=pass");return 0;}
  if(test=="stage-overrun") {
   const uint8_t data[]={1,2,3,4,5};assert(!stage.append(data,5));
   assert(stage.state()==wirecheck::ExclusiveStage::State::Failed);
   HalStorage::MutationSnapshot after;assert(storage.beginMutationSnapshot(after));
   auto empty=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);
   assert(empty && empty.fileSize()==0 && empty.close());std::puts("stage_overrun_rejected=pass");return 0;
  }
  if(test=="stage-short") {
   const uint8_t partial[]={7,8};assert(stage.append(partial,2));assert(!stage.finish());
   assert(stage.state()==wirecheck::ExclusiveStage::State::Failed);
   HalStorage::MutationSnapshot after;assert(storage.beginMutationSnapshot(after));
   auto partialFile=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);uint8_t read[2]{};
   assert(partialFile && partialFile.read(read,2)==2 && std::memcmp(read,partial,2)==0 && partialFile.close());
   std::puts("partial_stage_preserved_and_closed=pass");return 0;
  }
  if(test=="stage-fault") {
   disk.writes=0;disk.failWriteAt=0;std::array<uint8_t,1024> data{};
   const bool wrote=stage.append(data.data(),data.size());const bool finished=wrote && stage.finish();
   assert(disk.writeInjected && !finished && stage.state()==wirecheck::ExclusiveStage::State::Failed);
   disk.failWriteAt=-1;std::puts("stage_fault_rejected=pass");return 0;
  }
  const uint8_t data[]={1,2,3,4};assert(stage.append(data,4));assert(stage.finish());
  assert(stage.state()==wirecheck::ExclusiveStage::State::Complete);
  SDCard.volume.end();assert(SDCard.volume.begin(&device));
  auto f=SDCard.volume.open((std::string("/")+stageName).c_str(),O_RDONLY);uint8_t read[4]{};
  assert(f && f.read(read,4)==4 && std::memcmp(data,read,4)==0 && f.close());
  auto old=SDCard.volume.open("/Books/5 unique book.epub",O_RDONLY);assert(old && old.read()==int('x') && old.close());
  auto state=SDCard.volume.open("/.crosspoint/progress.dat",O_RDONLY);char saved[5];
  assert(state && state.read(saved,5)==5 && std::memcmp(saved,"state",5)==0 && state.close());
  std::puts("stage_created_and_existing_book_preserved=pass");return 0;
 }
 if(test=="inventory" || test=="inventory-change" || test=="inventory-paths") {
  ScanState state;wirecheck::InventorySession session(storage,{&state,keepGoing,emitIncluded});
  if(test=="inventory-paths") {
   const char* unsafe[]={"/Books/../.crosspoint","/Books//x","/Books/","Books","/.crosspoint",
                         "/Books/CROSSP~1","/Books/ bad","/Books/trailing.","/Books/\xff",
                         "/XTCache","/xtcache","/System Volume Information","/system volume information"};
   for(const char* path:unsafe){wirecheck::InventorySession bad(storage,{&state,keepGoing,emitEntry});
    assert(!bad.beginDirectory(path));assert(bad.state()==wirecheck::InventorySession::State::Failed);}
   assert(session.beginDirectory("/"));wirecheck::ScanResult root=wirecheck::ScanResult::Pending;
   for(unsigned i=0;i<2000 && root==wirecheck::ScanResult::Pending;++i)root=session.step();
   assert(root==wirecheck::ScanResult::Complete && state.entries==1 && state.excluded==3 && session.snapshotValid());
   assert(session.beginDirectory("/Books"));root=wirecheck::ScanResult::Pending;
   for(unsigned i=0;i<2000 && root==wirecheck::ScanResult::Pending;++i)root=session.step();
   assert(root==wirecheck::ScanResult::Complete && state.entries==42 && session.snapshotValid());
   std::puts("unsafe_paths_rejected=pass");return 0;
  }
  assert(session.beginDirectory("/Books"));wirecheck::ScanResult result=wirecheck::ScanResult::Pending;
  for(unsigned i=0;i<2000 && result==wirecheck::ScanResult::Pending;++i)result=session.step();
  assert(result==wirecheck::ScanResult::Complete && state.entries==41 && session.snapshotValid());
  if(test=="inventory-change") {
   auto writer=storage.open("/Books/changed.epub",O_CREAT|O_WRONLY);assert(writer.close());
   assert(!session.beginDirectory("/Books/zz cache"));
   assert(session.state()==wirecheck::InventorySession::State::Failed);
   std::puts("inter_directory_change_rejected=pass");return 0;
  }
  assert(session.beginDirectory("/Books/zz cache"));result=wirecheck::ScanResult::Pending;
  for(unsigned i=0;i<2000 && result==wirecheck::ScanResult::Pending;++i)result=session.step();
  assert(result==wirecheck::ScanResult::Complete && state.entries==41 && session.snapshotValid());
  std::puts("multi_directory_snapshot=pass");return 0;
 }
 if(test=="adapter" || test=="adapter-change" || test=="adapter-fault" || test=="adapter-cancel" ||
    test=="adapter-unknown" || test=="adapter-stale") {
  auto dir=storage.open("/Books",O_RDONLY);assert(dir);
  if(test=="adapter-unknown") SDCard.initialized=false;
  if(test=="adapter-stale") {auto writer=storage.open("/Books/stale.epub",O_CREAT|O_WRONLY);assert(writer.close());}
  ScanState state;wirecheck::StorageDirectoryScan scan(storage,snapshot,std::move(dir),{&state,keepGoing,emitEntry});
  if(test=="adapter-change") {
    for(unsigned i=0;i<100 && !state.entries;++i)assert(scan.step()==wirecheck::ScanResult::Pending);
    assert(state.entries>0);
    auto writer=storage.open("/Books/another.epub",O_CREAT|O_WRONLY);assert(writer.close());
  } else if(test=="adapter-fault") {disk.reads=0;disk.failAt=0;}
  else if(test=="adapter-cancel") state.running=false;
  wirecheck::ScanResult result=wirecheck::ScanResult::Pending;
  for(unsigned i=0;i<2000 && result==wirecheck::ScanResult::Pending;++i)result=scan.step();
  if(test=="adapter") {assert(result==wirecheck::ScanResult::Complete);assert(state.entries==41);}
  else {assert(result==wirecheck::ScanResult::Failed);if(test=="adapter-fault")assert(disk.injected);}
  std::printf("adapter=%s entries=%u injected=%d\n",test.c_str(),state.entries,disk.injected);return 0;
 }
 if(test=="mkdir-existing" || test=="mkdir-skip" || test=="mkdir-skip-fault") {
  const char* path="/Books/zz cache";disk.reads=0;
  if(test=="mkdir-skip-fault")disk.failAt=fault;
  if(test=="mkdir-existing") assert(!storage.mkdir(path,true));
  else if(!storage.exists(path)) storage.mkdir(path,true);
  const bool unchanged=storage.mutationSnapshotUnchanged(snapshot);
  if(disk.injected)assert(!unchanged);
  else assert(unchanged);
  std::printf("reads=%d injected=%d unchanged=%d\n",disk.reads,disk.injected,unchanged);return 0;
 }
 if(test=="mkdir-missing") {
  const char* path="/Books/new cache";
  if(!storage.exists(path))assert(storage.mkdir(path,true));
  assert(storage.exists(path));assert(!storage.mutationSnapshotUnchanged(snapshot));
  std::puts("missing_directory_created=pass");return 0;
 }
 if(test=="noops") {
  assert(!storage.mkdir("/Books",true));assert(!storage.remove("/Books/absent"));assert(storage.mutationSnapshotUnchanged(snapshot));
  auto file=storage.open("/Books/New.epub",O_CREAT|O_WRONLY);const uint8_t x=7;assert(file.write(&x,1)==1);assert(file.close());
  assert(!storage.mutationSnapshotUnchanged(snapshot));std::puts("noops_and_mutation=pass");return 0;
 }
 if(test=="stale-scan") {
  auto dir=storage.open("/Books",O_RDONLY);uint8_t record[32];
  assert(storage.readDirectoryRecord(dir,snapshot,record)==HalStorage::SnapshotRead::Record);
  auto writer=storage.open("/Books/Another.epub",O_CREAT|O_WRONLY);const uint8_t value=1;
  assert(writer.write(&value,1)==1);assert(writer.close());
  assert(storage.readDirectoryRecord(dir,snapshot,record)==HalStorage::SnapshotRead::Changed);
  assert(dir.close());std::puts("stale_scan_rejected=pass");return 0;
 }
 if(test=="write") {
  disk.writes=0;disk.failWriteAt=fault;
  auto writer=storage.open("/Books/Another.epub",O_CREAT|O_WRONLY);
  bool ok=writer.impl->file.isOpen();const uint8_t value=1;
  if(ok)ok=writer.write(&value,1)==1 && writer.sync();
  const bool closeOk=writer.close();ok=ok && closeOk;
  const bool injected=disk.writeInjected;disk.failWriteAt=-1;
  HalStorage::MutationSnapshot next;const bool available=storage.beginMutationSnapshot(next);
  if(injected && !ok)assert(!available);
  bool readback=false;
  if(ok) {
    SDCard.volume.end();assert(SDCard.volume.begin(&device));
    auto verify=SDCard.volume.open("/Books/Another.epub",O_RDONLY);uint8_t got=0;
    readback=verify && verify.read(&got,1)==1 && got==1 && verify.close();
  }
  std::printf("injected=%d ok=%d available=%d writes=%d readback=%d\n",injected,ok,available,disk.writes,readback);return 0;
 }
 if(test=="remove") {
  disk.reads=0;disk.failAt=fault;bool result=storage.remove("/Books/5 unique book.epub");
  const bool injected=disk.injected;disk.failAt=-1;
  const bool unchanged=storage.mutationSnapshotUnchanged(snapshot);
  if(unchanged){assert(!result);assert(SDCard.volume.exists("/Books/5 unique book.epub"));}
  if(result)assert(!unchanged);
  std::printf("injected=%d unchanged=%d result=%d reads=%d\n",injected,unchanged,result,disk.reads);return 0;
 }
 if(test=="read") {
  auto dir=storage.open("/Books",O_RDONLY);assert(dir.impl->file.isDirectory());
  disk.reads=0;disk.failAt=fault;uint8_t record[32];unsigned total=0;HalStorage::SnapshotRead result;
  do {result=storage.readDirectoryRecord(dir,snapshot,record);++total;}while(result==HalStorage::SnapshotRead::Record&&record[0]!=0&&total<1000);
  const bool injected=disk.injected;disk.failAt=-1;
  if(injected){assert(result==HalStorage::SnapshotRead::Failed || result==HalStorage::SnapshotRead::Changed);assert(!storage.mutationSnapshotUnchanged(snapshot));}
  else assert(result==HalStorage::SnapshotRead::End || (result==HalStorage::SnapshotRead::Record && record[0]==0));
  assert(dir.close());std::printf("injected=%d records=%u reads=%d\n",injected,total,disk.reads);return 0;
 }
 return 2;
}
'''

flags = ['-std=c++20', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
         '-DFREEINK_DEVICE_X4PRO=1',
         '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
         '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
         '-DDESTRUCTOR_CLOSES_FILE=1']
if os.environ.get('CROSSPOINT_DIAG_WRITE_FENCE') == '1':
    flags.append('-DEXPERIMENT_WRITE_FENCE=1')
if os.environ.get('CROSSPOINT_DIAG_LEGACY_CLOSE') == '1' or os.environ.get('CROSSPOINT_DIAG_LEGACY_SWEEP') == '1':
    flags.append('-DEXPERIMENT_LEGACY_CLOSE=1')
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    sdfat = dependency / 'src'
    sync_headers = ROOT / 'src/network/sync'
    if os.environ.get('CROSSPOINT_DIAG_CACHE_ACCESS') == '1':
        sdfat = tmp / 'sdfat'
        shutil.copytree(dependency / 'src', sdfat)
        if os.environ.get('CROSSPOINT_DIAG_EXFAT_SEPARATE') == '1':
            config = sdfat / 'SdFatConfig.h'
            config_text = config.read_text()
            needle = '#define USE_EXFAT_BITMAP_CACHE 0'
            assert config_text.count(needle) == 1
            config.write_text(config_text.replace(needle, '#define USE_EXFAT_BITMAP_CACHE 1'))
            flags.append('-DEXPECT_EXFAT_SEPARATE=1')
        for relative, start, prepare, count in (
                ('FatLib/FatPartition.h', 'm_fatStartSector', 'fatCachePrepare',
                 '(static_cast<uint64_t>(clusterCount())+2)*(fatType()==16?2:4)'),
                ('ExFatLib/ExFatPartition.h', 'm_clusterHeapStartSector', 'bitmapCachePrepare',
                 '(static_cast<uint64_t>(clusterCount())+7)/8')):
            target = sdfat / relative
            text = target.read_text()
            needle = '  Sector_t fatStartSector() const { return m_fatStartSector; }\n'
            assert text.count(needle) == 1
            extra = '''
  uint8_t* experimentalAllocation(uint32_t offset, uint8_t options) {
    if (FORMAT_GUARD) return nullptr;
    const uint64_t bytes = COUNT;
    if (offset >= (bytes + 511) / 512 || offset > UINT32_MAX - START) return nullptr;
    return PREPARE(START + offset, options);
  }
  bool experimentalReadAllocation(uint32_t offset, uint8_t* out) {
    if (!out) return false;
    if (FORMAT_GUARD) return false;
    const uint64_t bytes = COUNT;
    if (offset >= (bytes + 511) / 512 || offset > UINT32_MAX - START) return false;
    SAFE_READ
  }
  bool experimentalFlipAllocationByte(uint32_t offset, unsigned byte) {
    if (byte >= 512) return false;
    auto* data = experimentalAllocation(offset, FsCache::CACHE_FOR_WRITE);
    if (!data) return false;
    data[byte] ^= 1;
    return true;
  }
'''.replace('COUNT', count).replace('START', start).replace('PREPARE', prepare).replace(
                'FORMAT_GUARD', '!fatType()' if relative.startswith('ExFat') else 'fatType() != 16 && fatType() != 32')
            if relative.startswith('ExFat'):
                extra = extra.replace('    const uint64_t bytes =',
                                      '    if (m_bytesPerSector != 512) return nullptr;\n    const uint64_t bytes =', 1)
                extra = extra.replace('SAFE_READ', '''if (m_bytesPerSector != 512) return false;
#if USE_EXFAT_BITMAP_CACHE
    return m_bitmapCache.cacheSafeRead(m_clusterHeapStartSector + offset, out);
#else
    return m_dataCache.cacheSafeRead(m_clusterHeapStartSector + offset, out);
#endif''')
                extra += '''
  bool experimentalReadFat(uint32_t offset, uint8_t* out) {
    if (!fatType() || !out || m_bytesPerSector != 512 || offset >= m_fatLength ||
        offset > UINT32_MAX - m_fatStartSector) return false;
    return m_dataCache.cacheSafeRead(m_fatStartSector + offset, out);
  }
  bool experimentalReadMetadataSector(uint32_t sector, uint8_t* out) {
    return fatType() && m_bytesPerSector == 512 && out && m_dataCache.cacheSafeRead(sector, out);
  }
  bool experimentalFlipFatByte(uint32_t offset, unsigned byte) {
    if (!fatType() || m_bytesPerSector != 512 || offset >= m_fatLength ||
        offset > UINT32_MAX - m_fatStartSector || byte >= 512) return false;
    auto* data = dataCachePrepare(m_fatStartSector + offset, FsCache::CACHE_FOR_WRITE);
    if (!data) return false;
    data[byte] ^= 1;
    return true;
  }
'''
            else:
                extra = extra.replace('SAFE_READ', '''#if USE_SEPARATE_FAT_CACHE
    return m_fatCache.cacheSafeRead(m_fatStartSector + offset, out);
#else
    return m_cache.cacheSafeRead(m_fatStartSector + offset, out);
#endif''')
            target.write_text(text.replace(needle, needle + extra))
        target = sdfat / 'FsLib/FsVolume.h'
        text = target.read_text()
        needle = '  /** \\return free cluster count or -1 if an error occurs. */\n'
        assert text.count(needle) == 1
        extra = '''  bool experimentalReadAllocation(uint32_t offset, uint8_t* out) {
    return m_fVol ? m_fVol->experimentalReadAllocation(offset, out)
                  : m_xVol ? m_xVol->experimentalReadAllocation(offset, out) : false;
  }
  bool experimentalFlipAllocationByte(uint32_t offset, unsigned byte) {
    return m_fVol ? m_fVol->experimentalFlipAllocationByte(offset, byte)
                  : m_xVol ? m_xVol->experimentalFlipAllocationByte(offset, byte) : false;
  }
  bool experimentalReadFat(uint32_t offset, uint8_t* out) {
    return m_xVol && m_xVol->experimentalReadFat(offset, out);
  }
  bool experimentalReadMetadataSector(uint32_t sector, uint8_t* out) {
    return m_xVol && m_xVol->experimentalReadMetadataSector(sector, out);
  }
  bool experimentalFlipFatByte(uint32_t offset, unsigned byte) {
    return m_xVol && m_xVol->experimentalFlipFatByte(offset, byte);
  }
'''
        target.write_text(text.replace(needle, extra + needle))
        flags.append('-DEXPERIMENT_CACHE_READ=1')
    if os.environ.get('CROSSPOINT_DIAG_READ_DISCARD') == '1':
        sdfat = tmp / 'sdfat'
        shutil.copytree(dependency / 'src', sdfat)
        if os.environ.get('CROSSPOINT_DIAG_EXFAT_CLOSE_FIX') == '1':
            target = sdfat / 'ExFatLib/ExFatFile.cpp'
            text = target.read_text()
            before = '''  bool rtn = sync();
  m_attributes = FILE_ATTR_CLOSED;'''
            after = '''  bool rtn = sync();
  if (!rtn) return false;
  m_attributes = FILE_ATTR_CLOSED;'''
            assert text.count(before) == 1
            target.write_text(text.replace(before, after))
            target = sdfat / 'FsLib/FsFile.cpp'
            text = target.read_text()
            before = '''  bool rtn = m_fFile ? m_fFile->close() : m_xFile ? m_xFile->close() : true;
  m_fFile = nullptr;'''
            after = '''  bool rtn = m_fFile ? m_fFile->close() : m_xFile ? m_xFile->close() : true;
  if (!rtn && m_xFile) return false;
  m_fFile = nullptr;'''
            assert text.count(before) == 1 and text.count('  close();\n') == 7
            target.write_text(text.replace(before, after).replace('  close();\n', '  if (!close()) return false;\n'))
        for relative in ('FatLib/FatFile.h', 'ExFatLib/ExFatFile.h'):
            target = sdfat / relative
            text = target.read_text()
            needle = '  bool close();\n'
            assert text.count(needle) == 1
            target.write_text(text.replace(needle, needle + '''
  int experimentalDiscardReadOnly() {
    if (!isOpen() || isWritable()) return 0;
    const int result = (m_flags & FILE_FLAG_DIR_DIRTY) ? 2 : 1;
    m_attributes = FILE_ATTR_CLOSED;
    m_flags = 0;
    return result;
  }
  void experimentalSetDirtyForTest() { m_flags |= FILE_FLAG_DIR_DIRTY; }
  void experimentalClearDirtyForTest() { m_flags &= ~FILE_FLAG_DIR_DIRTY; }
  bool experimentalIsDirtyForTest() const { return (m_flags & FILE_FLAG_DIR_DIRTY) != 0; }
'''))
        target = sdfat / 'FsLib/FsFile.h'
        text = target.read_text()
        needle = '  bool close();\n'
        assert text.count(needle) == 1
        target.write_text(text.replace(needle, needle + '''
  int experimentalDiscardReadOnly() {
    const int discarded = m_fFile ? m_fFile->experimentalDiscardReadOnly()
                                 : m_xFile ? m_xFile->experimentalDiscardReadOnly() : 0;
    if (!discarded) return 0;
    m_fFile = nullptr;
    m_xFile = nullptr;
    return discarded;
  }
  void experimentalSetDirtyForTest() {
    if (m_fFile) m_fFile->experimentalSetDirtyForTest();
    if (m_xFile) m_xFile->experimentalSetDirtyForTest();
  }
  void experimentalClearDirtyForTest() {
    if (m_fFile) m_fFile->experimentalClearDirtyForTest();
    if (m_xFile) m_xFile->experimentalClearDirtyForTest();
  }
  bool experimentalIsDirtyForTest() const {
    return m_fFile ? m_fFile->experimentalIsDirtyForTest()
                   : m_xFile ? m_xFile->experimentalIsDirtyForTest() : false;
  }
'''))
        flags.append('-DEXPERIMENT_READ_DISCARD=1')
        sync_headers = tmp / 'sync'
        shutil.copytree(ROOT / 'src/network/sync', sync_headers)
        sequential = subprocess.run(['git', 'show',
                                     '6ede0899afbad3353f3fc506f4402482f7b4dc45:src/network/sync/StageHash.h'],
                                    cwd=ROOT, check=True, capture_output=True, text=True).stdout
        assert sequential.count('self.storage.monitoredStorageHealthy()') == 1
        assert sequential.count('self.storage.captureMutationMarker(self.marker)') == 1
        sequential = sequential.replace('self.storage.monitoredStorageHealthy()',
                                        'self.storage.beginMutationSnapshot(self.snapshot)')
        sequential = sequential.replace('self.storage.captureMutationMarker(self.marker)',
                                        'self.storage.mutationSnapshotUnchanged(self.snapshot)')
        sequential = sequential.replace('self.storage.mutationMarkerUnchanged(self.marker)',
                                        'self.storage.mutationSnapshotUnchanged(self.snapshot)')
        sequential = sequential.replace('HalStorage::MutationMarker marker;', 'HalStorage::MutationSnapshot snapshot;')
        assert sequential.count('self.file.close()') == 1 and sequential.count('file.close()') == 2
        sequential = sequential.replace('self.file.close()', 'self.file.experimentalDiscardReadOnly()')
        sequential = sequential.replace('if (file) file.close();', 'if (file) file.experimentalDiscardReadOnly();')
        destructor = '  ~StageHash() { mbedtls_sha256_free(&hash); }'
        assert sequential.count(destructor) == 1
        sequential = sequential.replace(destructor,
                                       '  ~StageHash() { if (file) file.experimentalDiscardReadOnly(); mbedtls_sha256_free(&hash); }')
        (sync_headers / 'StageHash.h').write_text(sequential)
    sources = sum((sorted((sdfat / part).glob('*.cpp')) for part in ('FatLib', 'ExFatLib', 'FsLib', 'common')), [])
    if os.environ.get('CROSSPOINT_DIAG_INTENT_TRACE') == '1':
        sync_headers = tmp / 'intent-trace'
        shutil.copytree(ROOT / 'src/network/sync', sync_headers)
        store_path = sync_headers / 'PublicationIntentStore.h'
        store_text = store_path.read_text()
        for call in ('auto file = storage.open(PATH, O_CREAT | O_EXCL | O_WRONLY);',
                     'const bool written = file.write(r.scratch, r.length) == r.length;'):
            marker = 'std::fprintf(stderr, "TRACE before ' + call.split(' = ')[0] + '\\n");\n    '
            assert store_text.count(call) == 1
            store_text = store_text.replace(call, marker + call)
        after_write = 'const bool written = file.write(r.scratch, r.length) == r.length;\n    const bool closed = file.close();'
        assert store_text.count(after_write) == 1
        store_text = store_text.replace(after_write, after_write.replace('    const bool closed',
                                        '    std::fprintf(stderr, "TRACE before close\\n");\n    const bool closed'))
        if os.environ.get('CROSSPOINT_DIAG_INTENT_GUARD') == '1':
            call = 'const bool written = file.write(r.scratch, r.length) == r.length;'
            assert store_text.count(call) == 1
            store_text = store_text.replace(call, 'if (!storage.monitoredStorageHealthy()) { file.close(); '
                                            'r.result = Prepare::Uncertain; return true; }\n    ' + call)
        store_path.write_text(store_text)
        assert code.count('if(writeInjected)++writesAfterFailure;') == 1
        code = code.replace('if(writeInjected)++writesAfterFailure;',
                            'if(writeInjected){++writesAfterFailure;std::fprintf(stderr,"TRACE additional write\\n");}')
        code = code.replace('writeInjected=true;return false;',
                            'writeInjected=true;std::fprintf(stderr,"TRACE injected write failure\\n");return false;')
    harness, executable = tmp / 'hal.cpp', tmp / 'hal-test'
    harness.write_text(code)
    (tmp / 'HalStorage.h').write_text('#pragma once\n')
    (tmp / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
    (tmp / 'Serialization.h').write_text('#pragma once\n')
    if os.environ.get('CROSSPOINT_DIAG_LEGACY_UNGUARDED') == '1':
        legacy = (ROOT / 'src/util/CrossInkBookmarks.cpp').read_text()
        assert legacy.count('if (source.file) source.file.close();') == 2
        (tmp / 'CrossInkBookmarks.cpp').write_text(legacy.replace('if (source.file) source.file.close();',
                                                                 'source.file.close();'))
    (tmp / 'mbedtls').mkdir()
    sha_tree = ast.parse((ROOT / 'test/stage_http/test_stage_hash.py').read_text())
    sha_stub = next(ast.literal_eval(node.value) for node in sha_tree.body
                    if isinstance(node, ast.Assign)
                    and any(isinstance(t, ast.Name) and t.id == 'sha' for t in node.targets))
    sha_stub = sha_stub.replace('#include <cstdint>', '#include <cstdint>\n#include <vector>\ninline std::vector<uint8_t> hashObserved;')
    sha_stub = sha_stub.replace(' for(size_t i=0;i<n;i++)c->sum+=p[i];return 0;',
                                ' hashObserved.insert(hashObserved.end(),p,p+n);for(size_t i=0;i<n;i++)c->sum+=p[i];return 0;')
    (tmp / 'mbedtls/sha256.h').write_text(sha_stub)
    subprocess.run([os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-include', str(ROOT / 'test/sdfat_lfn_lookup/HostCompat.h'), '-I', str(sdfat),
                    '-I', str(tmp), '-I', str(ROOT / 'lib/hal'), '-I', str(ROOT / 'lib/FsHelpers'),
                    '-I', str(sync_headers), '-I', str(ROOT / 'lib/Serialization'), '-I', str(ROOT / 'src/util'),
                    str(harness), *map(str, sources), '-o', str(executable)], check=True)
    total_masked = 0
    if os.environ.get('CROSSPOINT_DIAG_LEGACY_SWEEP') == '1':
        for kind in ('fat', 'exfat'):
            caught = 0
            max_after = 0
            for fault in range(-1, 48):
                result = subprocess.run([str(executable), kind, 'legacy-read-sweep', str(fault)],
                                        check=True, capture_output=True, text=True)
                if 'injected=1' in result.stdout:
                    caught += 1
                    max_after = max(max_after, int(re.search(r'reads_after_failure=(\d+)', result.stdout).group(1)))
                elif fault >= 0:
                    break
            assert caught > 0
            print(kind, 'legacy read faults=', caught, 'max subsequent reads=', max_after, flush=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_LEGACY_CLOSE') == '1':
        for kind in ('fat', 'exfat'):
            for scenario in ('legacy-empty-close', 'legacy-lookup-fault'):
                subprocess.run([str(executable), kind, scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_CHECKED_READ_OPEN') == '1':
        for kind in ('fat', 'exfat'):
            for scenario in ('checked-read-open-allocation', 'checked-read-monitor-after-fault'):
                subprocess.run([str(executable), kind, scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_INTENT_TRACE') == '1':
        for kind in ('fat', 'exfat'):
            for fault in (0, 1):
                print(kind, 'write offset', fault, flush=True)
                subprocess.run([str(executable), kind, 'intent-store-sweep-write', str(fault)], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_INTENT_STORE') == '1':
        for kind in ('fat', 'exfat'):
            for scenario in ('intent-store', 'intent-store-corrupt', 'intent-store-write-fault', 'intent-store-read-fault'):
                print(kind, scenario, flush=True)
                subprocess.run([str(executable), kind, scenario, '-1'], check=True)
            for scenario in ('intent-store-sweep-read', 'intent-store-sweep-write'):
                print(kind, scenario, flush=True)
                for fault in range(24):
                    subprocess.run([str(executable), kind, scenario, str(fault)], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_GUARDED_LISTENER') == '1':
        for scenario in ('guarded-listener', 'guarded-listener-cancel', 'guarded-listener-stop-after-create'):
            print(scenario, flush=True)
            subprocess.run([str(executable), 'exfat', scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_GUARDED_CONNECTION') == '1':
        for scenario in ('guarded-connection', 'guarded-connection-reject', 'guarded-connection-lost', 'guarded-connection-cancel'):
            print(scenario, flush=True)
            subprocess.run([str(executable), 'exfat', scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_GUARDED_SOCKET') == '1':
        subprocess.run([str(executable), 'exfat', 'guarded-socket', '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_GUARDED_PUMP') == '1':
        for scenario in ('guarded-pump', 'guarded-pump-slow', 'guarded-pump-short',
                         'guarded-pump-cancel-idle', 'guarded-pump-cancel-partial'):
            print(scenario, flush=True)
            subprocess.run([str(executable), 'exfat', scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_GUARDED_RAW') == '1':
        for scenario in ('guarded-stage', 'guarded-stage-empty', 'guarded-stage-identity', 'guarded-stage-cancel',
                         'guarded-stage-stale', 'guarded-stage-flag-header'):
            print(scenario, flush=True)
            subprocess.run([str(executable), 'exfat', scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_STAGE_OWNER') == '1':
        for kind, scenario in [('exfat', name) for name in ('owner-stage', 'owner-stage-stale',
                'owner-stage-insufficient', 'owner-stage-bad-boot', 'owner-stage-cancel', 'owner-stage-terminal')] + [('fat', 'owner-stage')]:
            print(kind, scenario, flush=True)
            subprocess.run([str(executable), kind, scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_ROOT_CURSOR') == '1':
        for kind in ('fat', 'exfat'):
            for scenario in ('root-cursor', 'root-cursor-failed-writer'):
                print(kind, scenario, flush=True)
                subprocess.run([str(executable), kind, scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_STAGE_ROOT_FAILURE') == '1':
        subprocess.run([str(executable), 'exfat', 'stage-root-failed-writer', '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_VOLUME_GEOMETRY') == '1':
        for kind in ('fat', 'exfat'):
            subprocess.run([str(executable), kind, 'volume-geometry', '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_EXFAT_BOOT') == '1':
        for scenario in ('exfat-boot-valid', 'exfat-boot-two-fats', 'exfat-boot-dirty', 'exfat-boot-bad-checksum'):
            print(scenario, flush=True)
            subprocess.run([str(executable), 'exfat', scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_BITMAP_LAYOUT') == '1':
        scenarios = ['exfat-bitmap-layout', 'exfat-bitmap-layout-relocated', 'exfat-bitmap-layout-length',
                         'exfat-bitmap-layout-flags', 'exfat-bitmap-layout-missing',
                         'exfat-bitmap-layout-duplicate', 'exfat-bitmap-layout-chain',
                         'exfat-bitmap-layout-read-fault', 'exfat-bitmap-layout-mutation',
                         'exfat-bitmap-layout-cancel']
        scenarios += ['exfat-bitmap-layout-stage', 'exfat-bitmap-layout-stage-insufficient',
                          'exfat-bitmap-layout-stage-stale', 'exfat-bitmap-layout-stage-bad-layout',
                          'exfat-bitmap-layout-stage-bad-boot', 'exfat-bitmap-layout-stage-boot-fault',
                          'exfat-bitmap-layout-stage-boot-stale', 'exfat-bitmap-layout-stage-bad-mbr',
                          'exfat-bitmap-layout-stage-failed-writer', 'exfat-bitmap-layout-stage-bitmap-free']
        for scenario in scenarios:
            print(scenario, flush=True)
            subprocess.run([str(executable), 'exfat', scenario, '-1'], check=True)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_CACHE_ACCESS') == '1':
        for kind in ('fat', 'exfat'):
            for scenario in ('allocation-cache-coherence', 'allocation-cache-eviction-fault', 'allocation-cache-read-fault'):
                result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True)
                print(kind, scenario, 'allocation cache diagnostic:', result.returncode, result.stdout.strip(), result.stderr.strip())
                if result.returncode:
                    raise SystemExit(1)
        result = subprocess.run([str(executable), 'exfat', 'exfat-fat-cache', '-1'], capture_output=True, text=True)
        print('exfat FAT cache diagnostic:', result.returncode, result.stdout.strip(), result.stderr.strip())
        if result.returncode:
            raise SystemExit(1)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_HASH_CURSOR') == '1':
        for kind in ('fat', 'exfat'):
            for scenario in ('hash-position-seek', 'hash-position-cursor'):
                result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True)
                print(kind, 'hash position diagnostic:', result.returncode, result.stdout.strip(), result.stderr.strip())
                if result.returncode:
                    raise SystemExit(1)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_CLOSE_RETRY') == '1':
        for scenario in ('exfat-close-retry-prototype', 'exfat-hal-close-retry-prototype', 'exfat-close-retry-move-hazard'):
            result = subprocess.run([str(executable), 'exfat', scenario, '-1'], capture_output=True, text=True)
            print('exfat close retry:', result.returncode, result.stdout.strip(), result.stderr.strip())
            if result.returncode:
                raise SystemExit(1)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_EXFAT_SWEEP') == '1':
        caught = 0
        for scenario in ('exfat-sync-fault-sweep', 'exfat-sync-read-sweep'):
            for fault in range(12):
                result = subprocess.run([str(executable), 'exfat', scenario, str(fault)],
                                        capture_output=True, text=True)
                print('exfat sync sweep:', scenario, result.returncode, result.stdout.strip(), result.stderr.strip())
                if result.returncode:
                    raise SystemExit(1)
                caught += 'injected=1' in result.stdout
        assert caught
        print('exfat sync injected cases:', caught)
        raise SystemExit(0)
    if os.environ.get('CROSSPOINT_DIAG_READ_DISCARD') == '1':
        failures = 0
        for kind in ('fat', 'exfat'):
            for scenario in ('hash-read-only-clean', 'read-discard-writer-control', 'read-discard-failed-writer', 'read-discard-guards', 'read-discard-rejected-destructor', 'read-discard-dirty-poison', 'hash-directory-reject', 'hash-failed-writer-close', 'hash-destroy-failed-writer', 'hash-start-with-writer', 'hash-unmounted', 'hash-fragmented',
                             'hash-change', 'hash-after', 'hash-fault'):
                result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True)
                print(kind, scenario, 'read discard diagnostic:', result.returncode, result.stdout.strip(), result.stderr.strip())
                failures += result.returncode != 0
        raise SystemExit(1 if failures else 0)
    if os.environ.get('CROSSPOINT_DIAG_LONG_CREATE') == '1':
        failures = 0
        for kind in ('fat', 'exfat'):
            for scenario in ('space-closed-writer-cache-long', 'space-closed-writer-cache-long-nodelete'):
                result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True)
                print('diagnostic', kind, scenario, 'exit:', result.returncode, result.stdout.strip(), result.stderr.strip())
                failures += result.returncode != 0
        raise SystemExit(1 if failures else 0)
    if os.environ.get('CROSSPOINT_DIAG_ALIAS_STRESS') == '1':
        result = subprocess.run([str(executable), 'fat', 'alias-collision-stress', '-1'], capture_output=True, text=True)
        print('alias stress exit:', result.returncode, result.stdout.strip(), result.stderr.strip())
        raise SystemExit(1 if result.returncode else 0)
    if os.environ.get('CROSSPOINT_DIAG_HASH_CLOSE') == '1':
        for kind in ('fat', 'exfat'):
            result = subprocess.run([str(executable), kind, 'hash-failed-writer-close', '-1'], capture_output=True, text=True)
            print(kind, 'hash close diagnostic:', result.returncode, result.stdout.strip(), result.stderr.strip())
            if result.returncode:
                raise SystemExit(1)
        raise SystemExit(0)
    for kind in ('fat', 'exfat'):
        for scenario in ('adapter', 'adapter-change', 'adapter-fault', 'adapter-cancel', 'adapter-unknown', 'adapter-stale',
                         'inventory', 'inventory-change', 'inventory-paths', 'stage', 'stage-collision', 'stage-fault', 'stage-short', 'stage-read-fault', 'stage-overrun', 'stage-empty', 'stage-concurrent', 'stage-unrelated', 'stage-slot-reuse',
                         'stage-invalid', 'raw-stage', 'raw-short', 'raw-collision', 'raw-slot-reuse', 'raw-fault',
                         'hash-success', 'hash-change', 'hash-after', 'hash-fault', 'hash-size', 'hash-fragmented', 'hash-failed-writer-close', 'hash-start-with-writer', 'space-count', 'space-open-writer', 'space-closed-writer-cache',
                         'space-scan', 'space-scan-fault', 'space-scan-change', 'space-closed-writer-cache-long', 'space-closed-writer-cache-long-nodelete',
                         'reserved-stage', 'reserved-insufficient', 'reserved-stale',
                         'section-slot-reuse', 'section-direct-create', 'section-direct-commit', 'section-page-short-write'):
            result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{kind} {scenario}: {result.stdout}\n{result.stderr}')
            print(kind, result.stdout.strip())
        if kind == 'fat':
            result = subprocess.run([str(executable), kind, 'alias-collision-stress', '-1'], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{kind} alias-collision-stress: {result.stdout}\n{result.stderr}')
            print(kind, result.stdout.strip())
            result = subprocess.run([str(executable), kind, 'space-fat32-high', '-1'], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{kind} space-fat32-high: {result.stdout}\n{result.stderr}')
            print(kind, 'FAT32 reserved-high-bit case:', result.stdout.strip())
        else:
            for scenario in ('exfat-close-first-failure', 'exfat-persistent-close-failure'):
                result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True)
                if result.returncode:
                    raise AssertionError(f'{kind} {scenario}: {result.stdout}\n{result.stderr}')
                print(kind, scenario, result.stdout.strip())
            for scenario in ('exfat-sync-fault-sweep', 'exfat-sync-read-sweep'):
                caught = 0
                for fault in range(12):
                    result = subprocess.run([str(executable), kind, scenario, str(fault)], capture_output=True, text=True)
                    if result.returncode:
                        raise AssertionError(f'{kind} {scenario} {fault}: {result.stdout}\n{result.stderr}')
                    caught += 'injected=1' in result.stdout
                assert caught, scenario
                print(kind, scenario, 'passed; injected faults=', caught)
        caught = 0
        for fault in range(16):
            result = subprocess.run([str(executable), kind, 'section-direct-fault', str(fault)], capture_output=True, text=True)
            if result.returncode:
                raise AssertionError(f'{kind} section-direct-fault {fault}: {result.stdout}\n{result.stderr}')
            caught += 'injected=1' in result.stdout
            if 'injected=1' in result.stdout:
                print(kind, f'section-direct-fault {fault}:', result.stdout.strip())
        assert caught, (kind, 'section-direct-fault')
        print(kind, f'section-direct-fault: passed; injected faults={caught}')
        for scenario in ('mount', 'noops', 'stale-scan', 'mkdir-missing', 'mkdir-skip-fault', 'remove', 'read', 'write'):
            caught = 0
            masked = 0
            recovered_mount = 0
            for fault in ([-1] if scenario in ('noops', 'stale-scan', 'mkdir-missing') else range(-1, 32)):
                result = subprocess.run([str(executable), kind, scenario, str(fault)], capture_output=True, text=True)
                if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
                    raise AssertionError(f'{kind} {scenario} {fault}: {result.stdout}\n{result.stderr}')
                caught += 'injected=1' in result.stdout
                recovered_mount += 'injected=1 mounted=1 available=0' in result.stdout
                masked += 'injected=1 ok=1 available=1' in result.stdout
                if 'injected=1 ok=1 available=1' in result.stdout:
                    print(f'{kind} masked write failure at {fault}: {result.stdout.strip()}')
            if scenario not in ('noops', 'stale-scan', 'mkdir-missing'):
                assert caught, (kind, scenario)
            if scenario == 'mount' and kind == 'fat':
                assert recovered_mount, 'expected a recovered FAT mount with a hidden read error'
            print(f'{kind} {scenario}: passed; injected faults={caught}, masked={masked}')
            total_masked += masked
        reads = []
        for scenario in ('mkdir-existing', 'mkdir-skip'):
            result = subprocess.run([str(executable), kind, scenario, '-1'], capture_output=True, text=True, check=True)
            reads.append(int(result.stdout.strip().split()[0].split('=')[1]))
        assert reads[1] < reads[0], (kind, reads)
        print(f'{kind} existing-path sector reads: mkdir={reads[0]}, skip={reads[1]}')
    if total_masked:
        raise AssertionError(f'{total_masked} low-level write failures were hidden from the HAL guard')
