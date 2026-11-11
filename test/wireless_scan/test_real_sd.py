"""Exercise the experimental scanner on pinned SdFat FAT and exFAT images."""
import hashlib
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
dependency = ROOT / '.pio/libdeps/x4pro/SdFat'
source = dependency / 'src'
spec = importlib.util.spec_from_file_location('sdfat_patch', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
for _, relative, _, expected in patcher.PATCHES:
    assert hashlib.sha256((dependency / relative).read_bytes()).hexdigest() == expected
for kind in ('fat', 'exfat'):
    exfat = kind == 'exfat'
    library = 'ExFatLib' if exfat else 'FatLib'
    files = sorted((source / library).glob('*.cpp')) + sorted((source / 'common').glob('*.cpp'))
    flags = ['-std=c++20', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
             '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
             '-DSDFAT_FILE_TYPE=' + ('2' if exfat else '1'), '-DUSE_LONG_FILE_NAMES=1',
             '-DUSE_UTF8_LONG_NAMES=1', '-DDESTRUCTOR_CLOSES_FILE=1', '-DTEST_EXFAT=' + str(int(exfat))]
    with tempfile.TemporaryDirectory() as directory:
        executable = Path(directory) / 'test'
        subprocess.run([os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
                        '-include', str(ROOT / 'test/sdfat_lfn_lookup/HostCompat.h'),
                        '-I', str(source), '-I', str(ROOT / 'src/network/sync'),
                        str(ROOT / 'test/wireless_scan/mixed_probe.cpp'), *map(str, files),
                        '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
            raise AssertionError(result.stdout + result.stderr)
        print(kind, result.stdout.strip())
