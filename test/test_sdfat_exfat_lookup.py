"""Exercise scoped checked lookups against an in-memory exFAT volume."""
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
    target = source / 'ExFatLib/ExFatFile.cpp'
    expected = next(row[3] for row in patcher.PATCHES if row[1] == 'src/ExFatLib/ExFatFile.cpp')
    patches = project / 'scripts/sdfat_patches'
    for name in ('0011-exfat-strict-scan.patch', '0016-exfat-source-bounded-chain.patch', '0019-exfat-prior-bounded-chain.patch'):
        subprocess.run(['git', 'apply', '--reverse', str(patches / name)], cwd=dependency, check=True)
        patcher.apply_patches(project, dependency)
        assert hashlib.sha256(target.read_bytes()).hexdigest() == expected
    fixed = target.read_text()
    hook = '  if (fname && dir->m_strictLookup && !dir->validateStrictDirectory()) goto fail;\n'
    assert fixed.count(hook) == 1
    steps = [line.strip() for line in fixed.splitlines() if line.strip().startswith('checksum = static_cast<uint16_t>')]
    assert len(steps) == 2 and steps[0] == steps[1]
    arithmetic = Path(temporary) / 'checksum.cpp'
    arithmetic.write_text('''#include <cstdint>
int main() {
  for (uint32_t c = 0; c <= 65535; ++c) for (unsigned b = 0; b <= 255; ++b) {
    uint16_t checksum = c; uint8_t entry[] = {static_cast<uint8_t>(b)}; unsigned j = 0;
''' + steps[0] + '''
    const uint16_t expected = static_cast<uint16_t>(c / 2 + (c % 2) * 32768 + b);
    if (checksum != expected) return 1;
  }
}
''')
    arithmetic_binary = Path(temporary) / 'checksum'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), str(arithmetic), '-o', str(arithmetic_binary)],
                   check=True, capture_output=True, text=True)
    subprocess.run([str(arithmetic_binary)], check=True, capture_output=True, text=True)
    cpp_files = sorted((source / 'ExFatLib').glob('*.cpp')) + sorted((source / 'common').glob('*.cpp'))
    flags = ['-std=c++17', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
             '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
             '-DSDFAT_FILE_TYPE=2', '-DUSE_UTF8_LONG_NAMES=1', '-DDESTRUCTOR_CLOSES_FILE=1']
    command = [os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-include', str(ROOT / 'test/sdfat_lfn_lookup/HostCompat.h'), '-I', str(source),
               str(ROOT / 'test/sdfat_exfat_lookup/ExFatLookupTest.cpp'), *map(str, cpp_files)]
    for name, contents in [('fixed', fixed), ('strict_unfixed', fixed.replace(hook, '', 1))]:
        target.write_text(contents)
        executable = Path(temporary) / name
        subprocess.run([*command, '-o', str(executable)], check=True, capture_output=True, text=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
            raise AssertionError(result.stderr)
        if name == 'fixed':
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            print(result.stdout.strip())
        else:
            assert result.returncode == 4 and 'exFAT scope mismatch' in result.stderr, result.stdout + result.stderr
            print('strict_unfixed reproduces ' + result.stderr.strip())
