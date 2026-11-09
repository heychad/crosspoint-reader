"""Exercise the actual SDK write method on host FAT/exFAT with write faults."""
from pathlib import Path
import importlib.util
import os
import shutil
import subprocess
import tempfile

root = Path(os.environ.get('CROSSPOINT_WORKTREE', Path.cwd())).resolve()
sdk = root / 'freeink-sdk/libs/hardware/SDCardManager'
dep = root / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('patcher', root / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

def extract(text, signature):
    start = text.index(signature)
    first = text.index('{', start)
    depth = 0
    for pos in range(first, len(text)):
        depth += (text[pos] == '{') - (text[pos] == '}')
        if not depth:
            return text[start:pos + 1]
    raise ValueError(signature)

writer = extract((sdk / 'src/SDCardManager.cpp').read_text(), 'bool SDCardManager::writeFile(')
replace = extract((sdk / 'include/SDCardManager.h').read_text(), 'bool replaceFile(')
fixture = (root / 'test/stats_safe_replace_real/RealStatsReplaceTest.cpp').read_text()
prefix = fixture[:fixture.index('#ifndef STATS_PAYLOAD_SIZE')]
prefix = prefix.replace('#include <StatsSafeReplace.h>', '')
prefix = prefix.replace('#include <HalStorage.h>', '')
prefix = prefix.replace('#include <FsLib/FsVolume.h>', '#include <FsLib/FsVolume.h>\n#include <FsLib/FsFile.h>')
prefix = prefix.replace('int writes = 0, failAt = -1;',
                        'int writes = 0, failAt = -1; int reads = 0, failRead = -1; bool injectedRead = false;')
prefix = prefix.replace('if (dead) return false;\n    assert(n < sectorCount());',
                        'if (dead) return false;\n    if (reads++ == failRead) { injectedRead = true; return false; }\n    assert(n < sectorCount());', 1)
glue = r'''
struct String {
  std::string value;
  String(const char* s): value(s) {}
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }
  operator const char*() const { return value.c_str(); }
};
String operator+(const String& left, const char* right) { return String((left.value + right).c_str()); }
struct SerialType {
  explicit operator bool() const { return false; }
  void println(const char*) {}
  template<class... T> void printf(const char*, T...) {}
} Serial;
struct SDCardManager {
  bool initialized = true;
  FsVolume* volume;
  explicit SDCardManager(FsVolume& v): volume(&v) {}
  FsVolume& vol() { return *volume; }
''' + replace + r'''
  bool writeFile(const char*, const String&);
};
''' + writer
body = r'''
std::string read(FsVolume& volume, const char* path) {
 auto f=volume.open(path,O_RDONLY); if(!f) return "<missing>";
 std::string data(f.fileSize(), '\0');
 if(f.read(data.data(),data.size())!=(int)data.size() || !f.close()) return "<unreadable>";
 return data;
}
int main() {
 const std::string oldText="{\"old\":\""+std::string(2200,'o')+"\"}";
 const std::string newText="{\"new\":\""+std::string(2200,'n')+"\"}";
 for(bool exfat:{false,true}) {
  Disk baseline;uint8_t scratch[512];
  if(exfat){ExFatFormatter fmt;assert(fmt.format(&baseline,scratch));}
  else {FatFormatter fmt;assert(fmt.format(&baseline,scratch));}
  { FsVolume v;assert(v.begin(&baseline)); auto f=v.open("/settings.json",O_CREAT|O_EXCL|O_WRONLY);
    assert(f && f.write(oldText.data(),oldText.size())==oldText.size() && f.close()); }
  for(bool stop:{false,true}) {
   int tested=0, complete=0, lost=0, laterLost=0;
   for(int fault=-1;fault<128;++fault) {
    Disk disk;disk.sectors=baseline.sectors;bool ok;
    {FsVolume v;assert(v.begin(&disk));disk.writes=0;disk.failAt=fault;disk.failStop=stop;
     SDCardManager m(v);ok=m.writeFile("/settings.json",String(newText.c_str()));}
    if(fault>=0 && !disk.injected) break;
    disk.dead=false;disk.failAt=-1;FsVolume remount;assert(remount.begin(&disk));
    const auto primary=read(remount,"/settings.json"), temp=read(remount,"/settings.json.tmp");
    if(fault==-1){assert(ok && primary==newText && temp=="<missing>");continue;}
    ++tested;
    if(primary==oldText||primary==newText||temp==oldText||temp==newText) ++complete;
    else {++lost;std::printf("no-complete-copy format=%s mode=%s write=%d primary-size=%zu staged-size=%zu\n",
      exfat?"exFAT":"FAT",stop?"fail-stop":"transient",fault,primary.size(),temp.size());}
    const std::string extra(8192,'z');
    auto later=remount.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);
    assert(later && later.write(extra.data(),extra.size())==extra.size() && later.close());
    FsVolume after;assert(after.begin(&disk));
    const auto primaryAfter=read(after,"/settings.json"), tempAfter=read(after,"/settings.json.tmp");
    if(primaryAfter!=oldText && primaryAfter!=newText && tempAfter!=oldText && tempAfter!=newText) {
      ++laterLost;std::printf("no-complete-copy-after-allocation format=%s mode=%s write=%d\n",
        exfat?"exFAT":"FAT",stop?"fail-stop":"transient",fault);
    }
   }
   std::printf("%s %s write-faults=%d at-least-one-complete=%d no-complete=%d after-allocation-lost=%d\n",
     exfat?"exFAT":"FAT",stop?"fail-stop":"transient",tested,complete,lost,laterLost);
   assert(tested>0);
  }
  Disk withStage;withStage.sectors=baseline.sectors;
  {FsVolume v;assert(v.begin(&withStage));auto f=v.open("/settings.json.tmp",O_CREAT|O_EXCL|O_WRONLY);
   assert(f && f.write(newText.data(),newText.size())==newText.size() && f.close());}
  int readFaults=0, unchanged=0;
  for(int fault=-1;fault<128;++fault) {
   Disk disk;disk.sectors=withStage.sectors;bool ok;
   {FsVolume v;assert(v.begin(&disk));disk.reads=0;disk.failRead=fault;SDCardManager m(v);
    ok=m.writeFile("/settings.json",String("replacement"));}
   if(fault>=0 && !disk.injectedRead) break;
   disk.failRead=-1;FsVolume remount;assert(remount.begin(&disk));
   const auto primary=read(remount,"/settings.json"), temp=read(remount,"/settings.json.tmp");
   if(fault==-1){assert(!ok && primary==oldText && temp==newText);continue;}
   ++readFaults;
   if(primary==oldText && temp==newText) ++unchanged;
   else std::printf("existing-stage-changed format=%s read=%d primary-size=%zu staged-size=%zu\n",
     exfat?"exFAT":"FAT",fault,primary.size(),temp.size());
  }
  std::printf("%s read-faults-with-existing-stage=%d unchanged=%d\n",exfat?"exFAT":"FAT",readFaults,unchanged);
  assert(readFaults>0);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    project = tmp / 'project'
    copy = project / '.pio/libdeps/x4pro/SdFat'
    shutil.copytree(dep / 'src', copy / 'src')
    shutil.copy2(dep / 'library.properties', copy / 'library.properties')
    shutil.copytree(root / 'scripts/sdfat_patches', project / 'scripts/sdfat_patches')
    patcher.apply_patches(project, copy)
    source = copy / 'src'
    files = []
    for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common'):
        files += sorted((source / folder).glob('*.cpp'))
    cpp = tmp / 'test.cpp'
    cpp.write_text(prefix + glue + body)
    flags = ['-std=c++20', '-O1', '-fsanitize=address', '-DSPI_DRIVER_SELECT=3',
             '-DUSE_BLOCK_DEVICE_INTERFACE=1', '-DENABLE_ARDUINO_FEATURES=0',
             '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0', '-DSDFAT_FILE_TYPE=3',
             '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1', '-DDESTRUCTOR_CLOSES_FILE=1',
             '-DUSE_SEPARATE_FAT_CACHE=1']
    cmd = [os.environ.get('CXX', 'c++'), *flags, '-include', str(root / 'test/sdfat_lfn_lookup/HostCompat.h'),
           '-I', str(root / 'test/stats_safe_replace_real/stubs'), '-I', str(source), str(cpp),
           *map(str, files), '-o', str(tmp / 'probe')]
    subprocess.run(cmd, check=True)
    subprocess.run([str(tmp / 'probe')], check=True)
