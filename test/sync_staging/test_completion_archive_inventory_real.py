"""Exercise complete archive inventory with patched FAT/exFAT images."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preview_real', root / 'test/sync_staging/test_preview_real.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
code = fixture.code[:fixture.code.index('int main(int argc,char** argv){')]
code += '\n#include "CompletionArchiveInventory.h"\n' + (root / 'src/activities/reader/CompletionArchiveInventory.cpp').read_text()
code += r'''
int main(int argc,char** argv) {
 assert(argc==3);const bool exfat=std::string(argv[1])=="exfat";const std::string scenario=argv[2];
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch));}else{FatFormatter f;assert(f.format(&disk,scratch));}
 FsVolume volume;assert(volume.begin(&disk));SDCard.volume=&volume;
 assert(volume.mkdir("/.completion-history-000000",true) && volume.mkdir("/.completion-history-000002",true));
 if(scenario=="range")assert(volume.mkdir("/.completion-history-001024",true));
 if(scenario=="case")assert(volume.mkdir("/.COMPLETION-HISTORY-000003",true));
 if(scenario=="file"){auto f=volume.open("/.completion-history-000003",O_CREAT|O_EXCL|O_WRONLY);assert(f && f.sync() && f.close());}
 SDCard.volume=nullptr;volume.end();assert(volume.begin(&disk));SDCard.volume=&volume;
 if(scenario.starts_with("read-"))disk.failReadAt=disk.reads+std::stoi(scenario.substr(5));
 auto& storage=HalStorage::getInstance();readingstats::completionjournal::ArchiveInventory inventory{};inventory.fill(0xa5);
 const int writes=disk.writes;const auto result=readingstats::completionjournal::inventoryArchives(storage,inventory);
 using namespace readingstats::completionjournal;
 if(disk.injectedRead)assert(result!=ArchiveInventoryResult::Ready && inventory[0]==0xa5);
 else if(scenario=="range")assert(result==ArchiveInventoryResult::Unavailable && inventory[0]==0xa5);
 else if(scenario=="case" || scenario=="file")assert(result==ArchiveInventoryResult::Conflict && inventory[0]==0xa5);
 else {assert(result==ArchiveInventoryResult::Ready);assert(hasArchive(inventory,0) && !hasArchive(inventory,1) && hasArchive(inventory,2));}
 assert(disk.writes==writes);
 std::printf("%s %s: %s\n",argv[1],argv[2],disk.injectedRead?"injected":"not reached");
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HalStorage.h').write_text('#pragma once\n')
    (tmp / 'Memory.h').write_text('#pragma once\n')
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    hal = fixture.fixture.fixture.hal
    cmd = [os.environ.get('CXX', 'c++'), *hal.flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
           '-include', str(tmp / 'HostCompat.h'), '-I', str(tmp), '-I', str(root / 'lib/hal'), '-I', str(hal.dependency / 'src'),
           '-I', str(root / 'src/network/sync'), '-I', str(root / 'src'), '-I', str(root / 'test/sync_staging/stubs'),
           '-I', str(root / 'src/activities/reader'), '-I', str(root / 'lib/FsHelpers'), '-I', str(root / 'lib/Serialization'),
           str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(cmd, check=True)
    for fs in ('fat', 'exfat'):
        for scenario in ('clear', 'range', 'case', 'file', *(f'read-{n}' for n in range(24))):
            subprocess.run([str(tmp / 'test'), fs, scenario], check=True)
print('Archive inventory passed FAT/exFAT fixtures')
