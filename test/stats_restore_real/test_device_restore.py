"""Exercise device-restore production code with SdFat disk images and injected write faults."""
from pathlib import Path
import hashlib
import importlib.util
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
dependency = ROOT / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('sdfat_patch', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
for _, relative, _, expected in patcher.PATCHES:
    if hashlib.sha256((dependency / relative).read_bytes()).hexdigest() != expected:
        raise AssertionError(f'SdFat patch mismatch: {relative}')
source = dependency / 'src'
cpp_files = [p for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common') for p in sorted((source / folder).glob('*.cpp'))]
flags = ['-std=c++20', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
         '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
         '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
         '-DDESTRUCTOR_CLOSES_FILE=1', '-DUSE_SEPARATE_FAT_CACHE=1']
with tempfile.TemporaryDirectory() as temporary:
    temp = Path(temporary)
    (temp / 'HostCompat.h').write_text('class __FlashStringHelper;\n#include <Arduino.h>\n')
    exe = temp / 'restore'
    command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(temp / 'HostCompat.h'), '-I', str(ROOT / 'test/stats_safe_replace_real/stubs'),
               '-I', str(ROOT / 'lib/Memory'), '-I', str(ROOT / 'test/book_reading_stats_atomic/stubs'), '-I', str(ROOT / 'src/activities/reader'),
               '-I', str(ROOT / 'lib/hal'), '-I', str(source),
               str(ROOT / 'test/stats_restore_real/DeviceRestoreRealTest.cpp'),
               str(ROOT / 'src/activities/reader/DeviceStatsBackup.cpp'),
               str(ROOT / 'src/activities/reader/StatsSafeReplace.cpp'), *map(str, cpp_files), '-o', str(exe)]
    compiled = subprocess.run(command, capture_output=True, text=True)
    if compiled.returncode:
        raise AssertionError(compiled.stdout + compiled.stderr)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    if result.returncode or 'ERROR: AddressSanitizer' in result.stderr or 'runtime error:' in result.stderr:
        raise AssertionError(result.stdout + result.stderr + f'\nexit={result.returncode}')
    print(result.stdout.strip())
