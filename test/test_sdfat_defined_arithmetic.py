"""Check cached FAT directory reads and exFAT hashes under both sanitizers."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('arithmetic_hal', ROOT / 'test/storage_fence_integration/test_hal_real.py')
hal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hal)
hash_source = (hal.dependency / 'src/ExFatLib/ExFatName.cpp').read_text()
hash_functions = hash_source[hash_source.index('static char toUpper('):hash_source.index('bool ExFatFile::cmpName(')]
code = r'''
#include <ExFatLib/ExFatFormatter.h>
#include <FatLib/FatFormatter.h>
#include <FsLib/FsVolume.h>
#include <FsLib/FsFile.h>
#include <common/FsBlockDeviceInterface.h>
#include <common/upcase.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
bool monitorFault=false;
''' + hal.disk + '\n' + hash_functions + r'''
uint16_t rotate16(uint16_t value){return static_cast<uint16_t>((uint32_t{value}>>1)|(uint32_t{value}<<15));}
uint16_t byteHash(uint16_t start,uint8_t low,uint8_t high){
 const auto first=static_cast<uint16_t>(uint32_t{rotate16(start)}+low);
 return static_cast<uint16_t>(uint32_t{rotate16(first)}+high);
}
int main(int argc,char** argv){
 assert(argc==2);
 if(std::string(argv[1])=="hash"){
  constexpr uint16_t words[]={0,1,65,97,0xe9,0x4e2d,0xffff};
  constexpr uint8_t bytes[]={0,1,65,97,127,128,255};
  for(uint32_t initial=0;initial<=0xffff;++initial){
   const auto h=static_cast<uint16_t>(initial);
   for(auto c:bytes){const auto upper=static_cast<uint8_t>(c>='a'&&c<='z'?c-'a'+'A':c);
    assert(exFatHash(static_cast<char>(c),h)==byteHash(h,upper,0));}
   for(auto c:words){const auto upper=toUpcase(c);
    assert(exFatHash(c,h)==byteHash(h,upper&0xff,upper>>8));}
  }
  std::puts("All 65536 hash seeds matched independent 16-bit rotate/add results for ASCII and Unicode inputs");
  return 0;
 }
 Disk disk;uint8_t scratch[512];FatFormatter format;assert(format.format(&disk,scratch));
 FsVolume volume;assert(volume.begin(&disk));assert(volume.mkdir("/books",true));
 for(unsigned n=0;n<40;++n){const auto name="/books/Café reading book "+std::to_string(n)+".epub";
  auto f=volume.open(name.c_str(),O_CREAT|O_EXCL|O_WRONLY);assert(f);
  std::vector<uint8_t> data(2300,static_cast<uint8_t>(n));
  assert(f.write(data.data(),data.size())==data.size() && f.sync() && f.close());}
 volume.end();assert(volume.begin(&disk));
 for(unsigned n=0;n<40;++n){const auto name="/BOOKS/Café reading book "+std::to_string(n)+".EPUB";
  auto f=volume.open(name.c_str(),O_RDONLY);assert(f);
  std::vector<uint8_t> data(2300);assert(f.read(data.data(),data.size())==static_cast<int>(data.size()) && f.close());
  for(auto byte:data)assert(byte==n);}
 auto directory=volume.open("/books",O_RDONLY);assert(directory);unsigned count=0;
 while(true){FsFile entry;if(!entry.openNext(&directory,O_RDONLY))break;assert(entry.fileSize()==2300 && entry.close());++count;}
 assert(count==40 && directory.getError()==0 && directory.close());volume.end();
 std::puts("Cached FAT lookups and directory iteration preserved all 40 long-name files after remount");
}
'''

with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    (tmp / 'HostCompat.h').write_text('class __FlashStringHelper;\ninline unsigned long millis(){static unsigned long t=1;return t++;}\n')
    # Match the X4 Pro bytewise helper path rather than Mac ARM unaligned casts.
    command = [os.environ.get('CXX', 'c++'), *hal.flags, '-U__ARM_FEATURE_UNALIGNED',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined', '-fno-omit-frame-pointer',
               *shlex.split(os.environ.get('CXXFLAGS', '')), '-include', str(tmp / 'HostCompat.h'),
               '-I', str(hal.dependency / 'src'), str(tmp / 'test.cpp'), *map(str, hal.files), '-o', str(tmp / 'test')]
    subprocess.run(command, check=True)
    failures = []
    for scenario in ('fat', 'hash'):
        result = subprocess.run([str(tmp / 'test'), scenario], capture_output=True, text=True)
        print(result.stdout, end='')
        if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
            failures.append(f'{scenario}: exit={result.returncode}\n{result.stderr}')
    if failures:
        raise AssertionError('\n'.join(failures))
