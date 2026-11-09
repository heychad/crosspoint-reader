"""Check every pinned SdFat settings-chain migration against its expected bytes."""
from pathlib import Path
import importlib.util
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('patcher', root / 'scripts/patch_sdfat.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)
selected = {'src/FatLib/FatFile.h', 'src/FatLib/FatFile.cpp', 'src/FsLib/FsFile.h',
            'src/ExFatLib/ExFatFile.h', 'src/ExFatLib/ExFatFile.cpp'}
with tempfile.TemporaryDirectory() as directory:
    project = Path(directory) / 'project'
    dep = project / '.pio/libdeps/x4pro/SdFat'
    source = root / '.pio/libdeps/x4pro/SdFat'
    shutil.copytree(source / 'src', dep / 'src')
    shutil.copy2(source / 'library.properties', dep / 'library.properties')
    shutil.copytree(root / 'scripts/sdfat_patches', project / 'scripts/sdfat_patches')
    patcher.apply_patches(project, dep)
    checked = 0
    for patch_name, relative, upstream_hash, final_hash in patcher.PATCHES:
        if relative not in selected:
            continue
        candidates = [(upstream_hash, patch_name), *patcher.MIGRATIONS.get(relative, {}).items()]
        for old_hash, migration in candidates:
            patch = project / 'scripts/sdfat_patches' / migration
            subprocess.run(['git', 'apply', '--reverse', str(patch)], cwd=dep, check=True)
            assert patcher.digest(dep / relative) == old_hash, (relative, migration)
            patcher.apply_patches(project, dep)
            assert patcher.digest(dep / relative) == final_hash, (relative, migration)
            checked += 1
    print(f'Checked {checked} SdFat upstream and migration paths')
