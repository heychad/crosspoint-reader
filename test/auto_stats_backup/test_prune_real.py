"""Run production retention and HAL scans on real SdFat FAT/exFAT images."""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('fence_harness', ROOT / 'test/storage_fence_integration/test_hal_real.py')
harness = importlib.util.module_from_spec(spec)
spec.loader.exec_module(harness)
code = harness.code[:harness.code.index('int main(int argc,char** argv)')]
code = code.replace('int opens=0;', 'int opens=0; bool exists(const char* p){return volume->exists(p);} bool remove(const char* p){return volume->remove(p);}')
code = code.replace('bool observedSdmmcHealthy()const;', '''bool withStorageLock(bool (*fn)(void*),void* ctx);
 bool settingsChainEligible(const char*);bool distinctSettingsChains(const char*,const char*);bool remove(const char*);
 bool observedSdmmcHealthy()const;''')
code += '\n#include "SmallFileIdentity.h"\n'
for signature in ('bool HalStorage::withStorageLock(', 'bool HalStorage::settingsChainEligible(',
                  'bool HalStorage::distinctSettingsChains(', 'bool HalStorage::remove('):
    code += harness.extract(signature) + '\n'
code += r'''
#define Storage HalStorage::getInstance()
#include "DeviceStatsBackup.h"
#include "DeviceStatsRetention.h"
struct GlobalReadingStats {};
namespace readingstats {bool normalStatsAllowed(){return true;}}
struct StatsSessionLease {static bool anyActive(){return false;}};
namespace statsbackup {
// File decoding/checksums have separate production-code tests. This fixture
// isolates pruning, directory scans, chain ownership, and deletion faults.
BackupIntegrity inspectDeviceBackup(const std::string& name,GlobalReadingStats&){
 auto file=SDCard.open(("/.crossink-stats-backup/"+name).c_str());uint8_t value=0;
 if(!file)return BackupIntegrity::Unavailable;
 bool ok=file.read(&value,1)==1 && value==1;
 return file.close() && ok?BackupIntegrity::Checked:BackupIntegrity::Unavailable;
}
}
'''
production = (ROOT / 'src/activities/reader/DeviceStatsRetention.cpp').read_text()
production = '\n'.join(line for line in production.splitlines() if not line.startswith('#include'))
code += production
code += r'''
constexpr char current[]="stats_cp_20261110_A_0000000000000001.bin";
constexpr char primary[]="/.crosspoint/global_stats.bin";
std::string dated(int day){char path[100];std::snprintf(path,sizeof(path),
 "/.crossink-stats-backup/stats_cp_202611%02d_A_0000000000000001.bin",day);return path;}
void put(FsVolume& volume,const char* path,uint8_t value=1){auto file=volume.open(path,O_CREAT|O_EXCL|O_WRONLY);
 assert(file && file.write(value)==1 && file.sync() && file.close());}
void verify(FsVolume& volume,const char* path,uint8_t value){auto file=volume.open(path,O_RDONLY);uint8_t read=0;
 assert(file && file.read(&read,1)==1 && read==value && file.close());}
int main(int argc,char** argv){
 assert(argc==4);bool exfat=std::string(argv[1])=="exfat";std::string scenario=argv[2];int fault=std::stoi(argv[3]);
 Disk disk;uint8_t scratch[512];
 if(exfat){ExFatFormatter formatter;assert(formatter.format(&disk,scratch));}
 else {FatFormatter formatter;assert(formatter.format(&disk,scratch));}
 {
  FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
  assert(volume.mkdir("/.crosspoint",true) && volume.mkdir("/.crossink-stats-backup",true));
  put(volume,primary,77);
  for(int day=1;day<=10;++day)put(volume,dated(day).c_str());
  put(volume,"/.crossink-stats-backup/stats_cp_00000000_U_0000000000000001.bin",2);
  put(volume,"/.crossink-stats-backup/manual.bin",3);
  if(scenario=="directory")assert(volume.mkdir("/.crossink-stats-backup/nested"));
  if(scenario=="corrupt") {auto f=volume.open(dated(1).c_str(),O_WRONLY);assert(f.write(uint8_t{9})==1 && f.close());}
 }
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
 disk.writes=0;disk.failAt=(scenario=="writefault" || scenario=="writefault-stop")?fault:-1;
 disk.failStop=scenario=="writefault-stop";
 disk.failReadAt=scenario=="readfault"?disk.reads+fault:-1;
 auto result=statsbackup::pruneAutomaticDeviceBackups(current);
 if(scenario=="normal")assert(result.removed==3 && !result.deferred);
 if(scenario=="directory" || scenario=="corrupt")assert(result.removed==0 && result.deferred && disk.writes==0);
 bool injected=disk.injected || disk.injectedRead;
 if(injected)assert(result.deferred);
 disk.dead=false;disk.failAt=-1;disk.failReadAt=-1;monitorFault=false;SDCard.volume=nullptr;volume.end();
 assert(volume.begin(&disk));SDCard.volume=&volume;
 verify(volume,primary,77);
 verify(volume,dated(10).c_str(),1);
 verify(volume,"/.crossink-stats-backup/stats_cp_00000000_U_0000000000000001.bin",2);
 verify(volume,"/.crossink-stats-backup/manual.bin",3);
 for(int day=4;day<=10;++day)verify(volume,dated(day).c_str(),1);
 if(scenario=="directory")for(int day=1;day<=3;++day)verify(volume,dated(day).c_str(),1);
 std::printf("%s %s %d removed=%u injected=%d\n",argv[1],argv[2],fault,result.removed,injected);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    temp = Path(temporary)
    (temp / 'test.cpp').write_text(code)
    for header in ('HalStorage.h', 'Arduino.h', 'Memory.h'):
        (temp / header).write_text('#pragma once\n')
    (temp / 'Logging.h').write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
    (temp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\ninline void delay(unsigned){}\n')
    command = [os.environ.get('CXX', 'c++'), *harness.flags, '-include', str(temp / 'HostCompat.h'),
               '-I', str(temp), '-I', str(ROOT / 'lib/hal'), '-I', str(harness.dependency / 'src'),
               '-I', str(ROOT / 'src/network/sync'), '-I', str(ROOT / 'src/activities/reader'),
               '-I', str(ROOT / 'src'), '-I', str(ROOT / 'lib/FsHelpers'), str(temp / 'test.cpp'),
               *map(str, harness.files), '-o', str(temp / 'test')]
    subprocess.run(command, check=True)
    for filesystem in ('fat', 'exfat'):
        for scenario in ('normal', 'directory', 'corrupt', 'writefault', 'writefault-stop', 'readfault'):
            faults = range(12) if scenario in ('writefault', 'writefault-stop', 'readfault') else (-1,)
            for fault in faults:
                subprocess.run([str(temp / 'test'), filesystem, scenario, str(fault)], check=True)
print('Production retention preserved current/manual/undated snapshots and primary totals on FAT/exFAT')
