"""Check bounded routine state saves and interruption recovery on FAT/exFAT."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('state_hal', ROOT / 'test/storage_fence_integration/test_perbook_atomic_real.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
hal = fixture.hal
code = fixture.code[:fixture.code.index('\nclass PersistableStoreBase')]
code += '\n#define CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY 1\n#include "MoveReferenceSafeWrite.h"\n'
code += (ROOT / 'lib/Serialization/MoveReferenceSafeWrite.cpp').read_text()
code += r'''
constexpr char PRIMARY[]="/.crosspoint/state.json";
constexpr char TEMP[]="/.crosspoint/state.json.sleep-tmp";
constexpr char BACKUP[]="/.crosspoint/state.json.sleep-bak";
constexpr char HISTORY[]="/.crosspoint/state.json.sleep-history-0000";
constexpr char LAST_HISTORY[]="/.crosspoint/state.json.sleep-history-9999";
std::string state(int n){return "{\"readerActivityLoadCount\":"+std::to_string(n)+",\"openEpubPath\":\"/book.epub\",\"keep\":\""+std::string(2300,'k')+"\"}";}
void put(FsVolume& v,const char* p,const std::string& bytes){auto f=v.open(p,O_CREAT|O_EXCL|O_WRONLY);assert(f && f.write(bytes.data(),bytes.size())==bytes.size() && f.sync() && f.close());}
std::string read(FsVolume& v,const char* p){auto f=v.open(p,O_RDONLY);if(!f)return "<missing>";std::string bytes(f.fileSize(),'\0');assert(f.read(bytes.data(),bytes.size())==static_cast<int>(bytes.size()) && f.close());return bytes;}
void mount(FsVolume& v,Disk& d){d.failAt=-1;d.dead=false;monitorFault=false;assert(v.begin(&d));SDCard.volume=&v;auto& storage=HalStorage::getInstance();storage.~HalStorage();new(&storage)HalStorage;}
void unmount(FsVolume& v){SDCard.volume=nullptr;v.end();}
bool save(int n){const auto bytes=state(n);return movereference::writeSleepPreferences(bytes.data(),bytes.size());}
void historyUnchanged(FsVolume& v){assert(read(v,HISTORY)=="old history" && read(v,LAST_HISTORY)=="last history");assert(!v.exists("/.crosspoint/state.json.sleep-history-0001"));}
int main(int argc,char** argv){
 assert(argc==2);const bool exfat=std::string(argv[1])=="exfat";
 Disk baseline;uint8_t scratch[512];
 if(exfat){ExFatFormatter f;assert(f.format(&baseline,scratch));}else{FatFormatter f;assert(f.format(&baseline,scratch));}
 FsVolume v;mount(v,baseline);assert(v.mkdir("/.crosspoint",true));
 put(v,PRIMARY,state(0));put(v,HISTORY,"old history");put(v,LAST_HISTORY,"last history");
 put(v,"/.crosspoint/progress.bin","reading position");unmount(v);
 Disk repeated=baseline;mount(v,repeated);
 for(int n=1;n<=128;++n){assert(save(n));assert(read(v,PRIMARY)==state(n));assert(!v.exists(TEMP) && !v.exists(BACKUP));historyUnchanged(v);}
 unmount(v);mount(v,repeated);assert(read(v,PRIMARY)==state(128));historyUnchanged(v);unmount(v);
 for(bool stop:{false,true}){
  int failures=0;
  for(int fault=-1;fault<64;++fault){
   Disk d=baseline;mount(v,d);d.writes=0;d.failAt=fault;d.failStop=stop;
   const bool saved=save(1),injected=d.injected;unmount(v);mount(v,d);
   if(saved)assert(read(v,PRIMARY)==state(1));if(fault<0)assert(saved);
   if(injected)++failures;
   bool complete=false;for(auto p:{PRIMARY,TEMP,BACKUP}){auto bytes=read(v,p);complete|=bytes==state(0)||bytes==state(1);}
   assert(complete);const bool retried=save(2);put(v,"/later.bin",std::string(32768,'z'));unmount(v);mount(v,d);
   if(retried)assert(read(v,PRIMARY)==state(2));
   complete=false;for(auto p:{PRIMARY,TEMP,BACKUP}){auto bytes=read(v,p);complete|=bytes==state(0)||bytes==state(1)||bytes==state(2);}
   assert(complete && read(v,"/.crosspoint/progress.bin")=="reading position");historyUnchanged(v);unmount(v);
   if(fault>=0&&!injected)break;
  }
  assert(failures>0);std::printf("%s interrupted routine state writes preserved state/history/progress: %d (stop=%d)\n",argv[1],failures,stop);
 }
 std::printf("%s 128 routine saves kept history bounded and preserved old history\n",argv[1]);
}
'''

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HalStorage.h').write_text('#pragma once\n')
    (tmp / 'Memory.h').write_text('#pragma once\n')
    (tmp / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    command = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-U__ARM_FEATURE_UNALIGNED', '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp),
               '-I', str(ROOT / 'lib/hal'), '-I', str(ROOT / 'lib/Serialization'),
               '-I', str(hal.dependency / 'src'), '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
               str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(command, check=True)
    for filesystem in ('fat', 'exfat'):
        result = subprocess.run([str(tmp / 'test'), filesystem], capture_output=True, text=True)
        print(result.stdout, end='')
        if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
            raise RuntimeError(result.stderr + f'\nexit={result.returncode}')
