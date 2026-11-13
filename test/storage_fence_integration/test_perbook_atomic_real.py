"""Run the per-book writer and HAL against failed SdFat rename aliases."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('perbook_hal', ROOT / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
code = hal.code[:hal.code.index('\n#include "StorageDirectoryScan.h"')]
code = code.replace('bool close();bool isOpen()const;explicit operator bool()const;',
                    'bool close();bool isOpen()const;bool isDirectory()const;uint64_t fileSize64();int read(void*,size_t);'
                    'size_t fileSize(){return fileSize64();}explicit operator bool()const;')
code = code.replace('int opens=0;', 'int opens=0;bool exists(const char* p){return volume->exists(p);}'
                    'bool remove(const char* p){return volume->remove(p);}'
                    'bool rename(const char* a,const char* b){return volume->rename(a,b);}')
lookup_stub = 'PathStatus lookupPath(const char* path){auto file=SDCard.open(path);if(!file)return PathStatus::Missing;return file.close()?PathStatus::Present:PathStatus::Failed;}'
assert lookup_stub in code
code = code.replace(lookup_stub, 'PathStatus lookupPath(const char*);bool withStorageLock(bool (*)(void*),void*);'
                    'bool settingsChainEligible(const char*);bool distinctSettingsChains(const char*,const char*);'
                    'bool remove(const char*);bool rename(const char*,const char*);'
                    'bool exists(const char* p){return SDCard.exists(p);}'
                    'bool openFileForRead(const char*,const std::string& p,HalFile& out){out=openChecked(p.c_str(),O_RDONLY);return bool(out);}'
                    'bool openFileForWrite(const char* m,const std::string& p,HalFile& out){return openFileForWrite(m,p.c_str(),out);}')
code += '\n#include "CheckedStoragePath.h"\n#include "SmallFileIdentity.h"\n#include <ArduinoJson.h>\n'
code += '''
#define HAL_FILE_WRAPPED_CALL(method, ...) HalStorage::StorageLock lock; assert(impl); return impl->file.method(__VA_ARGS__);
#define HAL_FILE_FORWARD_CALL(method, ...) assert(impl); return impl->file.method(__VA_ARGS__);
#define HAL_STORAGE_MUTATION_CALL(method, ...) HalStorage::StorageLock lock; mutationFence.mutationAttempt(); const bool ok=SDCard.method(__VA_ARGS__); if(!ok)mutationFence.poison(); return ok;
'''
code += '\n'.join(hal.extract(signature) for signature in (
    'bool HalFile::isDirectory() const', 'uint64_t HalFile::fileSize64()', 'int HalFile::read(void*',
    'HalStorage::PathStatus HalStorage::lookupPath(', 'bool HalStorage::withStorageLock(',
    'bool HalStorage::settingsChainEligible(', 'bool HalStorage::distinctSettingsChains(',
    'bool HalStorage::remove(', 'bool HalStorage::rename('))
code += '''
HalStorage& Storage=HalStorage::getInstance();
class PersistableStoreBase {public:static bool writeDocToFileAtomic(const char*,const JsonDocument&,size_t);};
'''
source_path = Path(os.environ.get('X4_ATOMIC_SOURCE', ROOT / 'lib/Serialization/PersistableStore.cpp'))
source = source_path.read_text()
code += source[source.index('namespace {\nbool matchesFile') if 'namespace {\nbool matchesFile' in source
               else source.index('namespace {\nenum class FileMatch'):
               source.index('\nbool PersistableStoreBase::writeDocToFileForMove(')]
code += r'''
constexpr char PRIMARY[]="/.crosspoint/reader_prefs.json";
constexpr char TEMP[]="/.crosspoint/reader_prefs.json.tmp";
constexpr char BACKUP[]="/.crosspoint/reader_prefs.json.bak";
std::string text(int value){return "{\"version\":1,\"overrides\":{\"fontSize\":"+std::to_string(value)+"},\"keep\":\""+std::string(2300,'k')+"\"}";}
void put(FsVolume& v,const char* p,const std::string& bytes){auto f=v.open(p,O_CREAT|O_EXCL|O_WRONLY);assert(f && f.write(bytes.data(),bytes.size())==bytes.size() && f.sync() && f.close());}
std::string read(FsVolume& v,const char* p){auto f=v.open(p,O_RDONLY);if(!f)return "<missing>";std::string bytes(f.fileSize(),'\0');assert(f.read(bytes.data(),bytes.size())==static_cast<int>(bytes.size()) && f.close());return bytes;}
void mount(FsVolume& v,Disk& d){d.failAt=-1;d.failReadAt=-1;d.dead=false;monitorFault=false;assert(v.begin(&d));SDCard.volume=&v;auto& storage=HalStorage::getInstance();storage.~HalStorage();new(&storage)HalStorage;}
void unmount(FsVolume& v){SDCard.volume=nullptr;v.end();}
bool save(int value){JsonDocument doc;const auto bytes=text(value);assert(!deserializeJson(doc,bytes));return PersistableStoreBase::writeDocToFileAtomic(PRIMARY,doc,4096);}
void allocateAndCheck(FsVolume& v,Disk& d,const char* a,const char* b,const std::string& expected){
 put(v,"/later.bin",std::string(32768,'z'));unmount(v);mount(v,d);
 assert(read(v,a)==expected && read(v,b)==expected);
 assert(read(v,"/.crosspoint/progress.bin")=="reading position");
}
int main(int argc,char** argv){
 assert(argc==2);const bool exfat=std::string(argv[1])=="exfat";
 Disk initial;uint8_t scratch[512];
 if(exfat){ExFatFormatter f;assert(f.format(&initial,scratch));}else{FatFormatter f;assert(f.format(&initial,scratch));}
 FsVolume v;mount(v,initial);assert(v.mkdir("/.crosspoint",true));put(v,"/.crosspoint/progress.bin","reading position");unmount(v);
 for(bool publishing:{false,true}){
  Disk baseline=initial;mount(v,baseline);put(v,publishing?BACKUP:PRIMARY,text(16));
  for(int n=0;n<64;++n){const auto p="/.crosspoint/a"+std::to_string(n);put(v,p.c_str(),"x");}
  put(v,TEMP,text(18));
  for(int n=0;n<64;++n){const auto p="/.crosspoint/b"+std::to_string(n);put(v,p.c_str(),"x");}
  unmount(v);
  int aliases=0;
  for(bool stop:{false,true})for(int fault=0;fault<40;++fault){
   Disk d=baseline;mount(v,d);d.writes=0;d.failAt=fault;d.failStop=stop;
   const bool renamed=v.rename(publishing?TEMP:PRIMARY,publishing?PRIMARY:BACKUP);
   const bool injected=d.injected;unmount(v);mount(v,d);
   const char* a=PRIMARY;const char* b=publishing?TEMP:BACKUP;
   auto left=v.open(a,O_RDONLY),right=v.open(b,O_RDONLY);
   const bool alias=left && right && left.firstCluster()!=0 && left.firstCluster()==right.firstCluster();
   if(left)assert(left.close());if(right)assert(right.close());
   if(alias){++aliases;assert(!renamed);const auto before=d.writes;
    assert(!save(20));assert(d.writes==before);
    allocateAndCheck(v,d,a,b,text(publishing?18:16));
   }
   unmount(v);if(!injected)break;
  }
  if(!aliases)std::fprintf(stderr,"no aliases found: %s publishing=%d\n",argv[1],publishing);
  assert(aliases>0);std::printf("%s %s aliases preserved after changed save and allocation: %d\n",argv[1],publishing?"publish":"backup",aliases);
 }
 Disk baseline=initial;mount(v,baseline);put(v,PRIMARY,text(16));unmount(v);
 int readFailures=0;
 for(int fault=0;fault<128;++fault){
  Disk d=baseline;mount(v,d);d.failReadAt=d.reads+fault;
  const bool saved=save(18);const bool injected=d.injectedRead;unmount(v);mount(v,d);
  if(injected)++readFailures;
  if(saved)assert(read(v,PRIMARY)==text(18));
  bool preserved=false;for(auto p:{PRIMARY,TEMP,BACKUP}){const auto bytes=read(v,p);preserved|=bytes==text(16)||bytes==text(18);}
  assert(preserved && read(v,"/.crosspoint/progress.bin")=="reading position");unmount(v);
  if(!injected)break;
 }
 assert(readFailures>0);std::printf("%s read failures preserved preferences: %d\n",argv[1],readFailures);
 for(bool stop:{false,true}){
  int injections=0;
  for(int fault=-1;fault<96;++fault){
   Disk d=baseline;mount(v,d);d.writes=0;d.failAt=fault;d.failStop=stop;
   const bool saved=save(18);const bool injected=d.injected;unmount(v);mount(v,d);
   bool preserved=false;for(auto p:{PRIMARY,TEMP,BACKUP}){const auto bytes=read(v,p);preserved|=bytes==text(16)||bytes==text(18);}
   assert(preserved);if(saved)assert(read(v,PRIMARY)==text(18));if(fault<0)assert(saved);
   if(injected)++injections;
   // Retry a different preference, then force later allocation and remount.
   const bool retried=save(20);put(v,"/later.bin",std::string(32768,'z'));unmount(v);mount(v,d);
   if(retried)assert(read(v,PRIMARY)==text(20));
   preserved=false;for(auto p:{PRIMARY,TEMP,BACKUP}){const auto bytes=read(v,p);preserved|=bytes==text(16)||bytes==text(18)||bytes==text(20);}
   assert(preserved && read(v,"/.crosspoint/progress.bin")=="reading position");unmount(v);
   if(fault>=0&&!injected)break;
  }
  assert(injections>0);std::printf("%s interrupted writes preserved a complete preference file: %d (stop=%d)\n",argv[1],injections,stop);
 }
}
'''

def main():
    with tempfile.TemporaryDirectory() as directory:
        tmp = Path(directory)
        (tmp / 'test.cpp').write_text(code)
        (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
        (tmp / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
        (tmp / 'Memory.h').write_text('#pragma once\n')
        command = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
                   '-U__ARM_FEATURE_UNALIGNED', '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp),
                   '-I', str(ROOT / 'lib/hal'), '-I', str(hal.dependency / 'src'),
                   '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'), str(tmp / 'test.cpp'),
                   *map(str, hal.files), '-o', str(tmp / 'test')]
        subprocess.run(command, check=True)
        for filesystem in ('fat', 'exfat'):
            result = subprocess.run([str(tmp / 'test'), filesystem], capture_output=True, text=True)
            print(result.stdout, end='')
            if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
                raise RuntimeError(result.stderr + f'\nexit={result.returncode}')

if __name__ == '__main__':
    main()
