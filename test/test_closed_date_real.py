"""Run the closed-book date edit using SdFat disk images and a host HAL adapter."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
dependency = root / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('patch_sdfat', root / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
for _, relative, _, expected in patcher.PATCHES:
    assert hashlib.sha256((dependency / relative).read_bytes()).hexdigest() == expected
source = dependency / 'src'
sd_sources = sum((sorted((source / folder).glob('*.cpp')) for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common')), [])
reader = root / 'src/activities/reader'
reader_sources = [reader / name for name in ('BookReadingStats.cpp', 'ReadingStatsStore.cpp', 'BookTracking.cpp',
                                               'StatsSafeReplace.cpp', 'ReadingStatsUtils.cpp')]
flags = ['-std=c++20', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
         '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
         '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
         '-DDESTRUCTOR_CLOSES_FILE=1']
if os.environ.get('CROSSPOINT_TEST_SHARED_CACHE') != '1':
    flags.append('-DUSE_SEPARATE_FAT_CACHE=1')
with tempfile.TemporaryDirectory() as folder:
    executable = Path(folder) / 'closed-date'
    command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(root / 'test/sdfat_lfn_lookup/HostCompat.h'),
               '-I', str(root / 'test/stats_safe_replace_real/stubs'),
               '-I', str(root / 'test/book_reading_stats_atomic/stubs'), '-I', str(reader),
               '-I', str(root / 'lib/hal'), '-I', str(source),
               str(root / 'test/stats_safe_replace_real/ClosedDateRealTest.cpp'),
               *map(str, reader_sources), *map(str, sd_sources), '-o', str(executable)]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    subprocess.run([str(executable)], check=True)
