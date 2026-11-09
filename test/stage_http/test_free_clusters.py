"""Check free-cluster counting and reserve arithmetic without storage I/O."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <limits>
#include "FreeClusterCounter.h"
using wirecheck::FreeClusterCounter;
using F=FreeClusterCounter::Format;using S=FreeClusterCounter::State;
int main(){
 {FreeClusterCounter c;std::array<uint8_t,512> b{};b[6]=5;b[10]=255;b[11]=255;
  assert(c.begin(F::Fat16,4,512) && c.consume(b.data(),b.size())==S::Complete);
  uint64_t free=0;assert(c.freeBytes(free) && free==1024);}
 {FreeClusterCounter c;std::array<uint8_t,512> b{};b[11]=0xf0;b[12]=1;
  assert(c.begin(F::Fat32,2,4096) && c.consume(b.data(),b.size())==S::Complete);
  uint64_t free=0;assert(c.freeBytes(free) && free==0);}
 {FreeClusterCounter c;std::array<uint8_t,512> b{};b.fill(255);b[0]=0xfe;b[1]=0xfd;
  assert(c.begin(F::ExFat,10,512) && c.consume(b.data(),b.size())==S::Complete);
  uint64_t free=0;assert(c.freeBytes(free) && free==1024);}
 {FreeClusterCounter c;std::array<uint8_t,512> b{};
  assert(c.begin(F::Fat16,300,512) && c.consume(b.data(),512)==S::Reading);
  assert(c.consume(b.data(),512)==S::Complete);uint64_t free=0;assert(c.freeBytes(free) && free==300*512);}
 {FreeClusterCounter c;std::array<uint8_t,512> b{};b.fill(255);
  assert(c.begin(F::ExFat,4097,512) && c.consume(b.data(),512)==S::Reading);
  b[0]=0xfe;assert(c.consume(b.data(),512)==S::Complete);uint64_t free=0;assert(c.freeBytes(free) && free==512);}
 {FreeClusterCounter c;std::array<uint8_t,512> b{};assert(c.begin(F::ExFat,10,512));
  assert(!c.allowsStage(0,0));assert(c.consume(b.data(),512)==S::Complete);
  assert(c.allowsStage(3072,1024) && !c.allowsStage(3073,1024));
  assert(!c.allowsStage(std::numeric_limits<uint64_t>::max(),0));
  assert(!c.allowsStage(0,std::numeric_limits<uint64_t>::max()));}
 {FreeClusterCounter c;assert(!c.begin(F::Fat32,0,512));}
 {FreeClusterCounter c;assert(!c.begin(F::Fat32,2,513));}
 {FreeClusterCounter c;assert(!c.begin(static_cast<F>(99),2,512));}
 {FreeClusterCounter c;assert(c.begin(F::Fat16,1,512));assert(c.consume(nullptr,512)==S::Failed);}
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    executable = Path(tmp) / 'test'
    source.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', str(ROOT / 'src/network/sync'),
                    str(source), '-o', str(executable)], check=True, timeout=30)
    subprocess.run([str(executable)], check=True, timeout=10)
print('Free cluster counting and reserve arithmetic passed')
