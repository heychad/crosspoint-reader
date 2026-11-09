"""Exercise reserve admission and the cancellation callback's lock boundary."""
import ast
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
tree = ast.parse((ROOT / 'test/stage_http/test_raw_session.py').read_text())
storage = next(ast.literal_eval(node.value) for node in tree.body
               if isinstance(node, ast.Assign)
               and any(isinstance(t, ast.Name) and t.id == 'STUB' for t in node.targets))
code = r'''
#include <cassert>
#include <cstring>
#include "ReservedStage.h"
using wirecheck::ReservedStage;using F=wirecheck::FreeClusterCounter::Format;
struct Context{HalStorage* storage;bool active=true;};
bool active(void* p){auto& c=*static_cast<Context*>(p);assert(c.storage->lockDepth==0);return c.active;}
bool readSector(void* p,uint32_t sector,uint8_t* out){auto& c=*static_cast<Context*>(p);
 assert(c.storage->lockDepth>0 && sector==0);std::memset(out,0,512);return true;}
constexpr const char* name="__xsync_0123456789abcdef0123456789abcdef.part";
int main(){
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  assert(stage.beginStage(name,4,65536));uint8_t bytes[]={1,2,3,4};assert(stage.append(bytes,4) && stage.finish());
  assert(d.bytes.size()==4 && std::memcmp(d.bytes.data(),bytes,4)==0 && d.lockDepth==0);}
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  assert(!stage.beginStage(name,65536,65536) && !d.exists);}
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  ++d.generation;assert(!stage.beginStage(name,4,65536) && !d.exists);}
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  c.active=false;assert(!stage.beginStage(name,4,65536) && !d.exists);}
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  d.beforeLock=[](void* p){static_cast<std::atomic_flag*>(p)->test_and_set();};d.beforeLockContext=&cancel;
  assert(!stage.beginStage(name,4,65536) && !d.exists);}
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  assert(stage.beginStage(name,4,65536));d.beforeLock=[](void* p){static_cast<std::atomic_flag*>(p)->test_and_set();};
  d.beforeLockContext=&cancel;const uint8_t bytes[]={1,2,3,4};assert(!stage.append(bytes,4) && d.bytes.empty());}
 {HalStorage d;Context c{&d};std::atomic_flag cancel{};ReservedStage stage(d,{0},{&c,active,readSector},cancel);
  assert(stage.beginScan({F::ExFat,0,256,512,1}) && stage.stepScan()==ReservedStage::State::Ready);
  const uint8_t bytes[]={1,2,3,4};assert(stage.beginStage(name,4,65536) && stage.append(bytes,4));
  d.beforeLock=[](void* p){static_cast<std::atomic_flag*>(p)->test_and_set();};d.beforeLockContext=&cancel;
  assert(!stage.finish() && stage.state()==ReservedStage::State::Failed && d.bytes.size()==4);}
}
'''
with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(storage)
    (directory / 'test.cpp').write_text(code)
    executable = directory / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'test.cpp'), '-o', str(executable)],
                   check=True, timeout=30)
    subprocess.run([str(executable)], check=True, timeout=10)
print('Reserved stage admission and callback lock boundary passed')
