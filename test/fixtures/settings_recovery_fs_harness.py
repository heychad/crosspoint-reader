"""Exercise the read-only settings probe on real in-memory FAT/exFAT volumes."""
from pathlib import Path
import importlib.util, os, shutil, subprocess, tempfile
root=Path(os.environ.get('CROSSPOINT_WORKTREE', Path.cwd())).resolve()
dep=root/'.pio/libdeps/x4pro/SdFat'
spec=importlib.util.spec_from_file_location('patcher',root/'scripts/patch_sdfat.py')
patcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(patcher)
fixture=(root/'test/stats_safe_replace_real/RealStatsReplaceTest.cpp').read_text()
prefix=fixture[:fixture.index('#ifndef STATS_PAYLOAD_SIZE')]
prefix=prefix.replace('#include <StatsSafeReplace.h>','#include <SettingsRecoveryProbe.h>')
prefix=prefix.replace('int writes = 0, failAt = -1;', 'int writes = 0, failAt = -1; int failRead = -1;')
prefix=prefix.replace('if (dead) return false;\n    assert(n < sectorCount());','if (dead) return false;\n    if (reads - 1 == failRead) { injectedRead = true; return false; }\n    assert(n < sectorCount());',1)
body=r'''
int main() {
 for (bool exfat : {false,true}) {
  Disk initial;uint8_t scratch[512];
  if(exfat){ExFatFormatter f;assert(f.format(&initial,scratch));}else{FatFormatter f;assert(f.format(&initial,scratch));}
  const std::string payload="{\"data\":\""+std::string(2268,'x')+"\"}";
  {
   FsVolume volume;assert(volume.begin(&initial));Storage.attach(volume);
   auto f=Storage.open("/settings.json", O_CREAT|O_EXCL|O_WRONLY);assert(f);
   assert(f.write(reinterpret_cast<const uint8_t*>(payload.data()),payload.size())==payload.size());assert(f.close());
  }
  int injections=0;
  for(int fault=-1;fault<128;++fault){
   Disk disk;disk.sectors=initial.sectors;FsVolume volume;assert(volume.begin(&disk));Storage.attach(volume);
   disk.reads=0;disk.writes=0;disk.failRead=fault;
   auto result=settingsrecovery::probe("/settings.json","/settings.bak","/settings.tmp");
   assert(disk.writes==0);
   if(fault==-1){assert(result.decision()==settingsrecovery::Plan::InspectPrimary);continue;}
   if(!disk.injectedRead)break;
   ++injections;
   if(result.decision()!=settingsrecovery::Plan::NeedsReview){
    std::fprintf(stderr,"hidden read failure format=%s offset=%d primary=%u backup=%u temp=%u\n",exfat?"exfat":"fat",fault,(unsigned)result.primary,(unsigned)result.backup,(unsigned)result.temporary);return 2;
   }
  }
  std::printf("%s settings bytes=%zu read-fault positions safely deferred=%d\n",exfat?"exFAT":"FAT",payload.size(),injections);
  assert(injections>0);
 }
}
'''
with tempfile.TemporaryDirectory() as d:
 tmp=Path(d);project=tmp/'project';copy=project/'.pio/libdeps/x4pro/SdFat';shutil.copytree(dep/'src',copy/'src');shutil.copy2(dep/'library.properties',copy/'library.properties');shutil.copytree(root/'scripts/sdfat_patches',project/'scripts/sdfat_patches');patcher.apply_patches(project,copy)
 stub=tmp/'stubs';stub.mkdir();h=(root/'test/stats_safe_replace_real/stubs/HalStorage.h').read_text();anchor='  size_t fileSize() const { return file_->fileSize(); }';assert h.count(anchor)==1;h=h.replace(anchor,anchor+'\n  uint64_t fileSize64() const { return file_->fileSize(); }\n  bool isDirectory() const { return file_->isDir(); }');anchor2='  bool openFileForRead(const char*, const char* path, HalFile& out) {';assert h.count(anchor2)==1;h=h.replace(anchor2,'  HalFile openChecked(const char* path, oflag_t flags) { return open(path, flags); }\n'+anchor2);(stub/'HalStorage.h').write_text(h)
 shutil.copy2(root/'test/settings_recovery_plan/stubs/Memory.h',stub/'Memory.h');cpp=tmp/'test.cpp';cpp.write_text(prefix+body)
 source=copy/'src';files=[]
 for folder in ('FsLib','FatLib','ExFatLib','common'):files+=sorted((source/folder).glob('*.cpp'))
 flags=['-std=c++20','-O1','-fsanitize=address','-DSPI_DRIVER_SELECT=3','-DUSE_BLOCK_DEVICE_INTERFACE=1','-DENABLE_ARDUINO_FEATURES=0','-DENABLE_ARDUINO_STRING=0','-DENABLE_ARDUINO_SERIAL=0','-DSDFAT_FILE_TYPE=3','-DUSE_LONG_FILE_NAMES=1','-DUSE_UTF8_LONG_NAMES=1','-DDESTRUCTOR_CLOSES_FILE=1','-DUSE_SEPARATE_FAT_CACHE=1']
 exe=tmp/'probe';cmd=[os.environ.get('CXX','c++'),*flags,'-include',str(root/'test/sdfat_lfn_lookup/HostCompat.h'),'-I',str(stub),'-I',str(root/'test/book_reading_stats_atomic/stubs'),'-I',str(root/'lib/Serialization'),'-I',str(root/'lib/hal'),'-I',str(root/'.cache/font-shortcut-host/_deps/arduinojson-src/src'),'-I',str(source),str(cpp),str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),*map(str,files),'-o',str(exe)]
 result=subprocess.run(cmd,capture_output=True,text=True)
 if result.returncode:raise RuntimeError(result.stdout+result.stderr)
 result=subprocess.run([str(exe)],capture_output=True,text=True)
 print(result.stdout,end='')
 if result.returncode:raise RuntimeError(result.stderr+f'\nexit={result.returncode}')
