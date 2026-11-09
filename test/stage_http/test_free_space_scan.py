"""Check incremental free-space scanning, cancellation and snapshot invalidation."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
stub = r'''
#pragma once
#include <cstdint>
struct HalStorage {
 struct MutationSnapshot{unsigned generation;};unsigned generation=0;
 bool mutationSnapshotUnchanged(const MutationSnapshot& s){return s.generation==generation;}
 bool withStorageLock(bool(*fn)(void*),void* ctx){return fn(ctx);}
};
'''
code = r'''
#include <cassert>
#include <cstring>
#include "FreeSpaceScan.h"
using wirecheck::FreeSpaceScan;using wirecheck::FreeClusterCounter;
using S=FreeSpaceScan::State;using F=FreeClusterCounter::Format;
struct Context{HalStorage* storage;bool active=true,readOk=true,mutate=false;unsigned reads=0;};
bool active(void* p){return static_cast<Context*>(p)->active;}
bool readSector(void* p,uint32_t sector,uint8_t* out){auto& c=*static_cast<Context*>(p);++c.reads;
 assert(sector==9 || sector==10);std::memset(out,0,512);if(c.mutate)++c.storage->generation;return c.readOk;}
int main(){
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{0},{&c,active,readSector});
  assert(scan.begin({F::Fat16,9,300,512,11}));assert(scan.step()==S::Reading && c.reads==1);
  assert(scan.step()==S::Complete && c.reads==2);uint64_t size=0;assert(scan.freeBytes(size) && size==300*512);
  assert(scan.allowsStage(1024,4096));++d.generation;assert(!scan.freeBytes(size) && !scan.allowsStage(0,0));}
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{0},{&c,active,readSector});
  assert(!scan.begin({F::Fat16,9,300,512,10}) && scan.state()==S::Failed && c.reads==0);}
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{0},{&c,active,readSector});
  assert(scan.begin({F::Fat16,9,1,512,10}));c.mutate=true;assert(scan.step()==S::Failed);}
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{0},{&c,active,readSector});
  assert(scan.begin({F::Fat16,9,1,512,10}));c.readOk=false;assert(scan.step()==S::Failed);}
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{0},{&c,active,readSector});
  assert(scan.begin({F::Fat16,9,1,512,10}));c.active=false;assert(scan.step()==S::Failed && c.reads==0);}
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{1},{&c,active,readSector});
  assert(!scan.begin({F::Fat16,9,1,512,10}));}
 {HalStorage d;Context c{&d};FreeSpaceScan scan(d,{0},{&c,active,readSector});
  assert(scan.begin({F::Fat16,9,1,512,10}));scan.cancel();assert(scan.step()==S::Failed);}
}
'''
with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(stub)
    (directory / 'test.cpp').write_text(code)
    executable = directory / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'test.cpp'), '-o', str(executable)],
                   check=True, timeout=30)
    subprocess.run([str(executable)], check=True, timeout=10)
print('Guarded incremental free-space scan passed')
