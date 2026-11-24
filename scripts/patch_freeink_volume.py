"""Expose the mounted filesystem for read-only queries in the pinned X4 Pro SDK."""
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
PATCH = Path('scripts/freeink_patches/0004-read-mounted-volume.patch')
ORIGINAL_SHA256 = '4d6d5ced3b8763b645519baf74c34a4385b78478766cce6fa79d2c28accdfed2'
PATCHED_SHA256 = '1c35cf6bf469c8f205defa1f7ae75ca9c31cf5839c1f31824ca6d0289024b72f'
PATCH_SHA256 = '58ef375fcb0e2693478af1ea857be0c3eb2a5ed9830b80591171317c47bd8b4a'


PREVIOUS_PATCHED_SHA256 = '8398495e33f83aae0a3e8dba5ea85a4cdc01af245b60b6fcc3a6a29c92a1f876'
UPGRADE_PATCH = Path('scripts/freeink_patches/0005-read-mounted-volume-from-test73.patch')
UPGRADE_PATCH_SHA256 = '48631e719fea7b5039613749079b6c18b57e0ad7606fb8beea448dad7ce97c13'


def apply_patch(project_dir, expected_revision=patcher.SDK_REVISION):
    sdk = Path(project_dir) / 'freeink-sdk'
    target = sdk / TARGET
    previous = (not sdk.is_symlink() and target.is_file() and patcher.within(target.resolve(), sdk.resolve())
                and not target.is_symlink() and patcher.digest(target) == PREVIOUS_PATCHED_SHA256)
    patcher.apply_patch(project_dir, expected_revision=expected_revision, target_path=TARGET,
                        patch_path=UPGRADE_PATCH if previous else PATCH, patched_sha=PATCHED_SHA256,
                        original_sha=PREVIOUS_PATCHED_SHA256 if previous else ORIGINAL_SHA256,
                        patch_sha=UPGRADE_PATCH_SHA256 if previous else PATCH_SHA256)


if 'Import' in globals():
    if env['PIOENV'].startswith('x4pro'):  # noqa: F821
        apply_patch(env['PROJECT_DIR'])  # noqa: F821
