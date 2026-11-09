"""Exercise the actual SdFat wrapper with scoped FAT and exFAT lookups."""
from pathlib import Path
import importlib.util
import hashlib
import os
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = ROOT / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('sdfat_patch', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
with tempfile.TemporaryDirectory() as temporary:
    project = Path(temporary) / 'project'
    dependency = project / '.pio/libdeps/x4pro/SdFat'
    shutil.copytree(DEPENDENCY / 'src', dependency / 'src')
    shutil.copy2(DEPENDENCY / 'library.properties', dependency / 'library.properties')
    shutil.copytree(ROOT / 'scripts/sdfat_patches', project / 'scripts/sdfat_patches')
    patcher.apply_patches(project, dependency)
    source = dependency / 'src'
    wrapper = source / 'FsLib/FsFile.h'
    expected = next(row[3] for row in patcher.PATCHES if row[1] == 'src/FsLib/FsFile.h')
    patches = project / 'scripts/sdfat_patches'
    for name in ('0008-fsfile-strict-lookup.patch', '0014-fsfile-bounded-chain.patch',
                 '0018-fsfile-prior-bounded-chain.patch', '0021-fs-header-settings-cluster.patch'):
        subprocess.run(['git', 'apply', '--reverse', str(patches / name)], cwd=dependency, check=True)
        patcher.apply_patches(project, dependency)
        assert hashlib.sha256(wrapper.read_bytes()).hexdigest() == expected
    cpp_files = []
    for folder in ('FsLib', 'FatLib', 'ExFatLib', 'common'):
        cpp_files.extend(sorted((source / folder).glob('*.cpp')))
    flags = ['-std=c++17', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
             '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
             '-DSDFAT_FILE_TYPE=3', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
             '-DDESTRUCTOR_CLOSES_FILE=1', '-DUSE_SEPARATE_FAT_CACHE=1']
    executable = Path(temporary) / 'wrapper'
    command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(ROOT / 'test/sdfat_lfn_lookup/HostCompat.h'), '-I', str(source),
               str(ROOT / 'test/sdfat_wrapper_lookup/FsWrapperLookupTest.cpp'), *map(str, cpp_files),
               '-o', str(executable)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True)
    if result.returncode or 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
        raise AssertionError(result.stdout + result.stderr + f'\nexit={result.returncode}')
    print(result.stdout.strip())
