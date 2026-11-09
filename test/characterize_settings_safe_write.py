from pathlib import Path
import os
base = Path(__file__).resolve().parent / 'fixtures/settings_recovery_fs_harness.py'
code = base.read_text()
code = code.replace("'#include <SettingsRecoveryProbe.h>'", "'#include <SettingsRecoveryProbe.h>\\n#include <SettingsSafeWrite.h>'")
start = code.index("body=r'''\n")
end = code.index("\n'''\nwith tempfile.TemporaryDirectory()", start) + 4
body = r'''body=r\x27\x27\x27
std::string read(FsVolume& volume, const char* path){auto f=volume.open(path,O_RDONLY);if(!f)return "<missing>";std::string s(f.fileSize(),'\0');if(f.read(s.data(),s.size())!=(int)s.size()||!f.close())return "<unreadable>";return s;}
int main(){
 const char* paths[]={"/.crosspoint/settings.json","/.crosspoint/settings.json.bak","/.crosspoint/settings.json.tmp"};
 const std::string oldText="{\"old\":\""+std::string(2200,'o')+"\"}", newText="{\"new\":\""+std::string(2200,'n')+"\"}";
 for(bool exfat:{false,true}){
  Disk baseline;baseline.sectorsTotal=64*1024*1024;uint8_t scratch[512];
  if(exfat){ExFatFormatter f;assert(f.format(&baseline,scratch));}else{FatFormatter f;assert(f.format(&baseline,scratch));}
  {FsVolume v;assert(v.begin(&baseline));assert(exfat||v.fatType()==32);Storage.attach(v);auto first=settingsrecovery::probe("/.crosspoint/settings.json","/.crosspoint/settings.json.bak","/.crosspoint/settings.json.tmp");assert(first.bootDecision().candidate==settingsrecovery::BootCandidate::Defaults);assert(v.mkdir("/.crosspoint"));auto f=v.open(paths[0],O_CREAT|O_EXCL|O_WRONLY);assert(f && f.write(oldText.data(),oldText.size())==oldText.size() && f.close());}
  {Disk empty;empty.sectorsTotal=baseline.sectorsTotal;empty.sectors=baseline.sectors;FsVolume v;assert(v.begin(&empty));assert(v.remove(paths[0]));Storage.attach(v);assert(settingsrecovery::writeSettings(newText.data(),newText.size()));assert(read(v,paths[0])==newText);assert(read(v,paths[1])=="<missing>"&&read(v,paths[2])=="<missing>");}
  for(bool stop:{false,true}){int injected=0,recovered=0;
   for(int fault=-1;fault<128;++fault){Disk disk;disk.sectorsTotal=baseline.sectorsTotal;disk.sectors=baseline.sectors;bool result;
    {FsVolume v;assert(v.begin(&disk));Storage.attach(v);disk.writes=0;disk.failAt=fault;disk.failStop=stop;result=settingsrecovery::writeSettings(newText.data(),newText.size());}
    if(fault>=0&&!disk.injected)break;if(disk.injected)++injected;
    disk.dead=false;disk.failAt=-1;FsVolume after;assert(after.begin(&disk));
    bool preserved=false;for(auto p:paths){auto value=read(after,p);if(value==oldText||value==newText)preserved=true;}
    if(!preserved){std::fprintf(stderr,"no complete settings: %s stop=%d fault=%d\n",exfat?"exfat":"fat32",stop,fault);return 2;}
    if(fault==-1 && (!result || read(after,paths[0])!=newText)){std::fprintf(stderr,"clean write failed: %s\n",exfat?"exfat":"fat32");return 3;}
    Storage.attach(after);const bool retried=settingsrecovery::writeSettings(newText.data(),newText.size());
    if(retried){assert(read(after,paths[0])==newText&&read(after,paths[1])=="<missing>"&&read(after,paths[2])=="<missing>");if(fault>=0)++recovered;}
    auto later=after.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);std::string more(8192,'z');assert(later&&later.write(more.data(),more.size())==more.size()&&later.close());
    FsVolume final;assert(final.begin(&disk));preserved=false;for(auto p:paths){auto value=read(final,p);if(value==oldText||value==newText)preserved=true;}if(!preserved){std::fprintf(stderr,"lost after allocation: %s stop=%d fault=%d\n",exfat?"exfat":"fat32",stop,fault);return 4;}
   }std::printf("%s %s write faults=%d preserved retry-success=%d\n",exfat?"exfat":"fat32",stop?"fail-stop":"transient",injected,recovered);assert(injected>0);
  }
  Disk interrupted;interrupted.sectorsTotal=baseline.sectorsTotal;interrupted.sectors=baseline.sectors;
  {FsVolume v;assert(v.begin(&interrupted));assert(v.rename(paths[0],paths[1]));auto temp=v.open(paths[2],O_CREAT|O_EXCL|O_WRONLY);assert(temp&&temp.write(newText.data(),newText.size())==newText.size()&&temp.close());}
  for(bool stop:{false,true}){int injected=0;
   for(int fault=-1;fault<128;++fault){Disk disk;disk.sectorsTotal=interrupted.sectorsTotal;disk.sectors=interrupted.sectors;bool result;
    {FsVolume v;assert(v.begin(&disk));Storage.attach(v);disk.writes=0;disk.failAt=fault;disk.failStop=stop;result=settingsrecovery::writeSettings(newText.data(),newText.size());}
    if(fault>=0&&!disk.injected)break;if(disk.injected)++injected;
    disk.dead=false;disk.failAt=-1;FsVolume after;assert(after.begin(&disk));bool preserved=false;
    for(auto p:paths){auto value=read(after,p);if(value==oldText||value==newText)preserved=true;}
    auto archived=read(after,"/.crosspoint/settings.recovered-01.bak");if(archived==oldText||archived==newText)preserved=true;
    if(!preserved){std::fprintf(stderr,"retry lost settings: %s stop=%d fault=%d\n",exfat?"exfat":"fat32",stop,fault);return 5;}
    if(fault==-1)assert(result&&read(after,paths[0])==newText&&archived==oldText);
    auto later=after.open("/later.bin",O_CREAT|O_EXCL|O_WRONLY);std::string more(8192,'z');assert(later&&later.write(more.data(),more.size())==more.size()&&later.close());
    FsVolume final;assert(final.begin(&disk));preserved=false;for(auto p:paths){auto value=read(final,p);if(value==oldText||value==newText)preserved=true;}
    archived=read(final,"/.crosspoint/settings.recovered-01.bak");if(archived==oldText||archived==newText)preserved=true;
    if(!preserved){std::fprintf(stderr,"retry lost settings after allocation: %s stop=%d fault=%d\n",exfat?"exfat":"fat32",stop,fault);return 6;}
   }std::printf("%s %s retry fault positions=%d preserved\n",exfat?"exfat":"fat32",stop?"fail-stop":"transient",injected);assert(injected>0);
  }
  Disk backupOnly;backupOnly.sectorsTotal=baseline.sectorsTotal;backupOnly.sectors=baseline.sectors;
  {FsVolume v;assert(v.begin(&backupOnly));assert(v.rename(paths[0],paths[1]));}
  for(bool stop:{false,true}){int injected=0;
   for(int fault=-1;fault<128;++fault){Disk disk;disk.sectorsTotal=backupOnly.sectorsTotal;disk.sectors=backupOnly.sectors;bool result;
    {FsVolume v;assert(v.begin(&disk));Storage.attach(v);disk.writes=0;disk.failAt=fault;disk.failStop=stop;result=settingsrecovery::writeSettings(oldText.data(),oldText.size());}
    if(fault>=0&&!disk.injected)break;if(disk.injected)++injected;
    disk.dead=false;disk.failAt=-1;FsVolume after;assert(after.begin(&disk));bool preserved=false;
    for(auto p:paths)if(read(after,p)==oldText)preserved=true;
    if(read(after,"/.crosspoint/settings.recovered-01.bak")==oldText)preserved=true;
    if(!preserved){std::fprintf(stderr,"backup-only lost settings: %s stop=%d fault=%d\n",exfat?"exfat":"fat32",stop,fault);return 7;}
    if(fault==-1)assert(result&&read(after,paths[0])==oldText&&read(after,"/.crosspoint/settings.recovered-01.bak")==oldText);
   }std::printf("%s %s backup-only fault positions=%d preserved\n",exfat?"exfat":"fat32",stop?"fail-stop":"transient",injected);assert(injected>0);
  }
 }
}
\x27\x27\x27'''.replace('\\x27', "'")
if os.environ.get('X4_SMALL_CLUSTER') == '1':
    body = body.replace("baseline.sectorsTotal=64*1024*1024", "baseline.sectorsTotal=exfat?1048576:131072")
    body = body.replace('v.fatType()==32', 'v.fatType()==16')
    body = body.replace('assert(exfat||v.fatType()==16);',
                        'assert(exfat||v.fatType()==16);assert(v.bytesPerCluster()<newText.size());')
    body = body.replace('"fat32"', '"fat16"')
    body = body.replace("std::string(2200,'o')", "std::string(16500,'o')")
    body = body.replace("std::string(2200,'n')", "std::string(16500,'n')")
code = code[:start] + body + code[end:]
code = code.replace("anchor+'\\n  uint64_t fileSize64()", "anchor+'\\n  size_t write(const void* p, size_t n) { return file_->write(p, n); }\\n  uint64_t fileSize64()")
code = code.replace("str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),*map", "str(root/'lib/Serialization/SettingsRecoveryProbe.cpp'),str(root/'lib/Serialization/SettingsSafeWrite.cpp'),*map")
if os.environ.get('X4_SMALL_CLUSTER') == '1':
    # The JSON parser's bounded allocator includes string copies. Keep the
    # serialized document multi-cluster with whitespace instead of a long value.
    code = code.replace('std::string(16500,\'o\')+"\\\"}"', 'std::string(1,\'o\')+"\\\"}"+std::string(16500,\' \')')
    code = code.replace('std::string(16500,\'n\')+"\\\"}"', 'std::string(1,\'n\')+"\\\"}"+std::string(16500,\' \')')
    anchor = "patcher.apply_patches(project,copy)"
    assert code.count(anchor) == 1
    code = code.replace(anchor, anchor + ";formatter=copy/'src/ExFatLib/ExFatFormatter.cpp';data=formatter.read_text();old='  sectorsPerClusterShift = vs < 29 ? 8 : (vs - 11) / 2;';assert data.count(old)==1;formatter.write_text(data.replace(old,'  sectorsPerClusterShift = 5;'))")
exec(compile(code, str(base), 'exec'), {'__file__': str(base), '__name__':'__main__'})
