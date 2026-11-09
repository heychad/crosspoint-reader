"""Exercise exclusive backup-file creation against pinned SdFat disk images."""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('filesystem', choices=('fat', 'exfat'))
args = parser.parse_args()
dependency = Path(os.environ.get('TEST_SDFAT_DIR', ROOT / '.pio/libdeps/x4pro/SdFat'))
spec = importlib.util.spec_from_file_location('sdfat_patch', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
for _, relative, _, expected in patcher.PATCHES:
    if hashlib.sha256((dependency / relative).read_bytes()).hexdigest() != expected:
        raise AssertionError(f'SdFat patch mismatch: {relative}')
source = dependency / 'src'
lib = 'FatLib' if args.filesystem == 'fat' else 'ExFatLib'
cpp_files = sorted((source / lib).glob('*.cpp')) + sorted((source / 'common').glob('*.cpp'))
flags = ['-std=c++20', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
         '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
         '-DUSE_UTF8_LONG_NAMES=1', '-DDESTRUCTOR_CLOSES_FILE=1']
if args.filesystem == 'fat':
    flags += ['-DSDFAT_FILE_TYPE=1', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_SEPARATE_FAT_CACHE=1']
    fixture = ROOT / 'test/stats_backup_real/FatDirectBackupTest.cpp'
else:
    flags += ['-DSDFAT_FILE_TYPE=2']
    fixture = ROOT / 'test/stats_backup_real/ExFatDirectBackupTest.cpp'
with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / 'backup-probe'
    command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(ROOT / 'test/sdfat_lfn_lookup/HostCompat.h'), '-I', str(source),
               str(fixture), *map(str, cpp_files), '-o', str(executable)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
        raise AssertionError(result.stdout + result.stderr)
    print(result.stdout.strip())
