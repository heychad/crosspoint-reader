"""Exercise the patched X4 Pro SDMMC monitor against extracted driver methods."""
from pathlib import Path
from fixture import patched_source
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = patched_source()

def function(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(signature)

monitor = source[source.index('namespace {\nstd::atomic<bool> syncMonitorArmed'):
                 source.index('\n#endif', source.index('namespace {\nstd::atomic<bool> syncMonitorArmed'))]
read = function('bool SdmmcBlockDevice::readSectors(')
write = function('bool SdmmcBlockDevice::writeSectors(')
code = r'''
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#define FREEINK_DEVICE_X4PRO 1
using Sector_t=uint32_t;
struct sdmmc_card_t {};
constexpr int ESP_OK=0;
int calls=0,failAt=-1;
int sdmmc_read_sectors(sdmmc_card_t*,void* dest,Sector_t,size_t count){
 if(calls++==failAt)return -1;std::memset(dest,0x6a,count*512);return ESP_OK;
}
int sdmmc_write_sectors(sdmmc_card_t*,const void*,Sector_t,size_t){return calls++==failAt?-1:ESP_OK;}
''' + monitor + r'''
namespace freeink {
struct SdmmcBlockDevice {
 void* _card=nullptr;uint8_t* _dmaBuffer=nullptr;
 static constexpr size_t kSectorSize=512,kMaxTransferSectors=8;
 bool readSectors(Sector_t,uint8_t*,size_t);bool writeSectors(Sector_t,const uint8_t*,size_t);
};
''' + read + '\n' + write + r'''
}
int main(int argc,char** argv){
 assert(argc==2);std::string mode=argv[1];sdmmc_card_t card;uint8_t dma[4096]{},buf[5120]{};
 freeink::SdmmcBlockDevice d;d._card=&card;d._dmaBuffer=dma;
 if(mode=="startup") {failAt=0;assert(!d.readSectors(0,buf,1));assert(crosspoint_sdmmc_monitor_healthy());crosspoint_sdmmc_monitor_arm();assert(crosspoint_sdmmc_monitor_healthy());}
 else {crosspoint_sdmmc_monitor_arm();assert(crosspoint_sdmmc_monitor_healthy());
  if(mode=="read") {failAt=1;assert(!d.readSectors(0,buf,10));}
  else if(mode=="write") {failAt=1;assert(!d.writeSectors(0,buf,10));}
  else if(mode=="invalid") {assert(!d.readSectors(0,nullptr,1));}
  else if(mode=="success") {assert(d.readSectors(0,buf,10));assert(d.writeSectors(0,buf,10));assert(crosspoint_sdmmc_monitor_healthy());return 0;}
  else return 2;
  assert(!crosspoint_sdmmc_monitor_healthy());crosspoint_sdmmc_monitor_arm();assert(!crosspoint_sdmmc_monitor_healthy());
 }
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    cpp, binary = tmp / 'monitor.cpp', tmp / 'monitor'
    cpp.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(cpp), '-o', str(binary)], check=True)
    for scenario in ('startup', 'read', 'write', 'invalid', 'success'):
        subprocess.run([str(binary), scenario], check=True)
print('X4 Pro SDMMC monitor catches post-mount read and write failures')
