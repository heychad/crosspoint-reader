"""Verify reviewed SdFat source versions converge and unknown versions fail closed."""
import hashlib
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
DEPENDENCY = ROOT / '.pio/libdeps/x4pro/SdFat'
spec = importlib.util.spec_from_file_location('patch_sdfat', ROOT / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


with tempfile.TemporaryDirectory() as directory:
    project = Path(directory)
    target = project / '.pio/libdeps/x4pro/SdFat'
    target.parent.mkdir(parents=True)
    shutil.copytree(DEPENDENCY, target)
    shutil.copytree(ROOT / 'scripts/sdfat_patches', project / 'scripts/sdfat_patches')
    relative = Path('src/FatLib/FatFileLFN.cpp')
    source = target / relative
    final_bytes = (DEPENDENCY / relative).read_bytes()
    expected = next(patched for _, path, _, patched in patcher.PATCHES if path == str(relative))
    assert digest(source) == expected
    for patch in ('0004-preserve-lfn-read-errors.patch', '0005-upgrade-lfn-create-guard.patch',
                  '0006-enable-strict-fat-lookup.patch', '0013-fix-sfn-alias-collision.patch'):
        source.write_bytes(final_bytes)
        subprocess.run(['git', 'apply', '--reverse', str(project / 'scripts/sdfat_patches' / patch)],
                       cwd=target, check=True)
        patcher.apply_patches(project, target)
        assert digest(source) == expected, patch
    exfat = target / 'src/ExFatLib/ExFatFileWrite.cpp'
    exfat_expected = next(patched for _, path, _, patched in patcher.PATCHES
                          if path == 'src/ExFatLib/ExFatFileWrite.cpp')
    subprocess.run(['git', 'apply', '--reverse',
                    str(project / 'scripts/sdfat_patches/0014-exfat-retain-dirty-on-sync-failure.patch')],
                   cwd=target, check=True)
    patcher.apply_patches(project, target)
    assert digest(exfat) == exfat_expected
    patcher.apply_patches(project, target)

    # A changed dependency must fail before the patcher alters any target.
    subprocess.run(['git', 'apply', '--reverse', str(project / 'scripts/sdfat_patches/0003-sync-fat-before-data-cache.patch')],
                   cwd=target, check=True)
    source.write_bytes(final_bytes + b'\n// unrecognized source\n')
    before = {path: digest(target / path) for _, path, _, _ in patcher.PATCHES}
    try:
        patcher.apply_patches(project, target)
    except RuntimeError as error:
        assert 'Unrecognized SdFat source' in str(error)
    else:
        raise AssertionError('Unrecognized source was accepted')
    assert before == {path: digest(target / path) for _, path, _, _ in patcher.PATCHES}

print('SdFat source migrations and unknown-source rejection passed')
