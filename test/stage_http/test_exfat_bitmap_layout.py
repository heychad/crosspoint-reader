"""Validate the allocation bitmap assumptions required by the sync scanner."""
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
#include "ExFatBitmapLayout.h"
using V=wirecheck::ExFatBitmapLayout;
void put(uint8_t* p,uint64_t n,int count){for(int i=0;i<count;i++)p[i]=static_cast<uint8_t>(n>>(i*8));}
std::array<uint8_t,32> entry(uint32_t clusters){std::array<uint8_t,32> e{};e[0]=0x81;
 put(e.data()+20,2,4);put(e.data()+24,(uint64_t(clusters)+7)/8,8);return e;}
int main(){
 {V v;auto e=entry(4097);assert(v.begin(4097,512,1));assert(v.record(e.data(),e.size()));
  assert(v.endRecords());assert(v.chainEntry(3));assert(v.state()==V::State::Chain);
  assert(v.chainEntry(0xffffffff));assert(v.state()==V::State::Complete);}
 {V v;auto e=entry(100);assert(v.begin(100,512,1));assert(v.record(e.data(),32));
  assert(v.endRecords() && v.chainEntry(0xffffffff));assert(v.state()==V::State::Complete);}
 {V v;auto e=entry(4097);assert(v.begin(4097,512,1) && v.record(e.data(),32) && v.endRecords());
  assert(!v.chainEntry(4) && v.state()==V::State::Failed);}
 {V v;auto e=entry(100);assert(v.begin(100,512,1) && v.record(e.data(),32));
  assert(!v.record(e.data(),32));}
 {V v;auto e=entry(100);e[1]=1;assert(v.begin(100,512,1) && !v.record(e.data(),32));}
 {V v;auto e=entry(100);e[20]=3;assert(v.begin(100,512,1) && !v.record(e.data(),32));}
 {V v;auto e=entry(100);e[24]++;assert(v.begin(100,512,1) && !v.record(e.data(),32));}
 {V v;assert(v.begin(100,512,1) && !v.endRecords());}
 {V v;assert(!v.begin(100,512,2));}
 {V v;auto e=entry(100);assert(v.begin(100,512,1) && v.record(e.data(),32) && v.endRecords());
  assert(!v.chainEntry(0));}
 {V v;auto e=entry(100);assert(v.begin(100,512,1) && v.record(e.data(),32) && v.endRecords());
  assert(!v.chainEntry(0xfffffff8));}
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
print('exFAT bitmap layout checks passed')
