"""Compile the non-X4 branch without the SDK's X4-only volume method."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'lib/hal/HalStorage.cpp').read_text()
start = source.index('HalStorage::SnapshotFormat HalStorage::snapshotFormat(')
opening = source.index('{', start)
depth = 0
for i in range(opening, len(source)):
    if source[i] == '{':
        depth += 1
    elif source[i] == '}':
        depth -= 1
        if depth == 0:
            method = source[start:i + 1]
            break
else:
    raise AssertionError('snapshotFormat')

code = r'''
#include <cassert>
#include "StorageMutationFence.h"
#define FREEINK_DEVICE_X4PRO 0
class HalStorage {
 checkedscan::StorageMutationFence mutationFence;
 public:
 enum class SnapshotFormat {Unknown,Fat,ExFat};
 using MutationSnapshot=checkedscan::StorageMutationFence::Snapshot;
 class StorageLock {public:~StorageLock(){}};
 SnapshotFormat snapshotFormat(const MutationSnapshot&);
 bool snapshot(MutationSnapshot& value){return mutationFence.beginSnapshot(value);}
};
''' + method + r'''
int main(){HalStorage storage;HalStorage::MutationSnapshot snapshot;
 assert(storage.snapshot(snapshot));
 assert(storage.snapshotFormat(snapshot)==HalStorage::SnapshotFormat::Unknown);
 assert(!storage.snapshot(snapshot));}
'''
with tempfile.TemporaryDirectory() as tmp:
    source_path, binary = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
    source_path.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', str(ROOT / 'lib/hal'),
                    str(source_path), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Non-X4 format query fails closed without the SDK extension')
