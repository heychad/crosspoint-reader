"""Expose mounted filesystem type and geometry through the pinned FreeInk SDK."""
import importlib.util
from pathlib import Path

if 'Import' in globals():
    Import('env')  # noqa: F821 -- supplied by SCons
    ROOT = Path(env['PROJECT_DIR'])  # noqa: F821
else:
    ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('patch_freeink_sdmmc', ROOT / 'scripts/patch_freeink_sdmmc.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)

TARGET = Path('libs/hardware/SDCardManager/include/SDCardManager.h')
PATCH = Path('scripts/freeink_patches/0002-mounted-volume-type.patch')
ORIGINAL_SHA256 = '4d6d5ced3b8763b645519baf74c34a4385b78478766cce6fa79d2c28accdfed2'
PATCHED_SHA256 = 'd4572bece9957e84c8e6b1c018404ab32fdd60134e4452cf891eb3841a87fd89'
PATCH_SHA256 = '45f58f4244c2750f5f4e4fbeaa80cd2f98a2574c19eced307c1d4568e8f545d4'
PREVIOUS_SHA256 = '47eda48c211b8b33210ac504838d71d77497099ecb7d2613b52df0d7a8397922'
MIGRATION_PATCH = Path('scripts/freeink_patches/0003-mounted-volume-geometry.patch')
MIGRATION_SHA256 = '79e55d6580edc1fa167ff74e313e56dfc4936847a03515a8b6c540880f761cdf'
GEOMETRY_SHA256 = '5a7dc65e55857ba4ae8f167cf399e97e3209c56d1ac98aaecb65fde7c2e61844'
GEOMETRY_MIGRATION_PATCH = Path('scripts/freeink_patches/0004-mounted-volume-readers.patch')
GEOMETRY_MIGRATION_SHA256 = '54acf31edca8b3000cc3d776ecbfbfb373e4c1cd56c7945b55933b43afea2e8a'


def apply_patch(project_dir, expected_revision=patcher.SDK_REVISION):
    project = Path(project_dir).resolve()
    sdk = project / 'freeink-sdk'
    target = sdk / TARGET
    if (not sdk.is_symlink() and target.is_file() and not target.is_symlink()
            and patcher.within(target.resolve(), sdk.resolve())
            and patcher.digest(target) in (PREVIOUS_SHA256, GEOMETRY_SHA256)):
        full_patch = project / PATCH
        if (not full_patch.is_file() or full_patch.is_symlink() or
                not patcher.within(full_patch.resolve(), project) or patcher.digest(full_patch) != PATCH_SHA256):
            raise RuntimeError('Missing or altered FreeInk volume patch')
        previous = patcher.digest(target) == PREVIOUS_SHA256
        patcher.apply_patch(project, expected_revision=expected_revision, target_path=TARGET,
                            patch_path=MIGRATION_PATCH if previous else GEOMETRY_MIGRATION_PATCH,
                            original_sha=PREVIOUS_SHA256 if previous else GEOMETRY_SHA256,
                            patched_sha=PATCHED_SHA256,
                            patch_sha=MIGRATION_SHA256 if previous else GEOMETRY_MIGRATION_SHA256)
        return
    patcher.apply_patch(project_dir, expected_revision=expected_revision, target_path=TARGET, patch_path=PATCH,
                        original_sha=ORIGINAL_SHA256, patched_sha=PATCHED_SHA256, patch_sha=PATCH_SHA256)


if 'Import' in globals():
    if env['PIOENV'].startswith('x4pro'):  # noqa: F821
        apply_patch(env['PROJECT_DIR'])  # noqa: F821
