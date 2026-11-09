"""Verify long-filename read-error propagation on a real in-memory FAT volume."""
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

with tempfile.TemporaryDirectory() as directory:
    project = Path(directory) / 'project'
    dependency = project / '.pio/libdeps/x4pro/SdFat'
    shutil.copytree(DEPENDENCY / 'src', dependency / 'src')
    shutil.copy2(DEPENDENCY / 'library.properties', dependency / 'library.properties')
    patches = project / 'scripts/sdfat_patches'
    shutil.copytree(ROOT / 'scripts/sdfat_patches', patches)
    patcher.apply_patches(project, dependency)
    source = dependency / 'src'
    # Exercise both a clean dependency and an installed older revision.
    lfn_path = source / 'FatLib/FatFileLFN.cpp'
    expected_patched = next(row[3] for row in patcher.PATCHES if row[1] == 'src/FatLib/FatFileLFN.cpp')
    for patch_name in ('0004-preserve-lfn-read-errors.patch', '0005-upgrade-lfn-create-guard.patch',
                       '0006-enable-strict-fat-lookup.patch'):
        subprocess.run(['git', 'apply', '--reverse', str(patches / patch_name)], cwd=dependency, check=True)
        patcher.apply_patches(project, dependency)
        assert hashlib.sha256(lfn_path.read_bytes()).hexdigest() == expected_patched
    cpp_files = sorted((source / 'FatLib').glob('*.cpp')) + sorted((source / 'common').glob('*.cpp'))
    flags = [
        '-std=c++17', '-O1', '-DSPI_DRIVER_SELECT=3', '-DUSE_BLOCK_DEVICE_INTERFACE=1',
        '-DENABLE_ARDUINO_FEATURES=0', '-DENABLE_ARDUINO_STRING=0', '-DENABLE_ARDUINO_SERIAL=0',
        '-DSDFAT_FILE_TYPE=1', '-DUSE_LONG_FILE_NAMES=1', '-DUSE_UTF8_LONG_NAMES=1',
        '-DDESTRUCTOR_CLOSES_FILE=1', '-DUSE_SEPARATE_FAT_CACHE=1',
    ]
    command = [
        os.environ.get('CXX', 'c++'), *flags, *shlex.split(os.environ.get('CXXFLAGS', '')),
        '-include', str(ROOT / 'test/sdfat_lfn_lookup/HostCompat.h'), '-I', str(source), '-I', str(ROOT / 'lib/hal'),
        str(ROOT / 'test/sdfat_lfn_lookup/SdFatLfnLookupTest.cpp'), *map(str, cpp_files),
    ]
    fixed = lfn_path.read_text()
    subprocess.run(['git', 'apply', '--reverse', str(patches / '0006-enable-strict-fat-lookup.patch')],
                   cwd=dependency, check=True)
    strict_unfixed = lfn_path.read_text()
    patcher.apply_patches(project, dependency)
    guard = '          if (dirFile->getError()) {\n            goto fail;\n          }\n'
    assert fixed.count(guard) == 1
    previous = strict_unfixed.replace(guard, '', 1)
    original = previous.replace('  m_error |= READ_ERROR;\n', '', 1)
    expected = next(row[2] for row in patcher.PATCHES if row[1] == 'src/FatLib/FatFileLFN.cpp')
    assert hashlib.sha256(original.encode()).hexdigest() == expected
    variants = {
        'fixed': fixed,
        'copied_error': fixed.replace('  m_error |= READ_ERROR;', '  m_error |= dir.getError();', 1),
        'stage_unfixed': previous,
        'original': original,
        'strict_unfixed': strict_unfixed,
    }
    for name, contents in variants.items():
        lfn_path.write_text(contents)
        executable = Path(directory) / name
        subprocess.run([*command, '-o', str(executable)], check=True, capture_output=True, text=True)
        result = subprocess.run([str(executable)], capture_output=True, text=True)
        if 'runtime error:' in result.stderr or 'ERROR: AddressSanitizer' in result.stderr:
            raise AssertionError(result.stderr)
        if name == 'fixed':
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            print(result.stdout.strip())
        elif name == 'strict_unfixed':
            assert result.returncode == 4 and 'corrupt LFN classified as' in result.stderr, result.stdout + result.stderr
            print(name + ' reproduces ' + result.stderr.strip())
        elif name == 'stage_unfixed':
            assert result.returncode == 3 and 'exclusive create accepted' in result.stderr, result.stdout + result.stderr
            print(name + ' reproduces ' + result.stderr.strip())
        else:
            assert result.returncode == 2 and 'hidden read failure' in result.stderr, result.stdout + result.stderr
            print(name + ' reproduces ' + result.stderr.strip())
print('Real fragmented FAT long-filename lookup regression passed')
