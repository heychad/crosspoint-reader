"""Expose the mounted filesystem type in the pinned X4 Pro SDK."""
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
PATCH = Path('scripts/freeink_patches/0002-mounted-filesystem-type.patch')
ORIGINAL_SHA256 = '4d6d5ced3b8763b645519baf74c34a4385b78478766cce6fa79d2c28accdfed2'
PATCHED_SHA256 = '8398495e33f83aae0a3e8dba5ea85a4cdc01af245b60b6fcc3a6a29c92a1f876'
PATCH_SHA256 = 'bec67f3b83037b211d9e1252ace21d3fa6ed2d5516da02a934f3858d6f2a0feb'


def apply_patch(project_dir, expected_revision=patcher.SDK_REVISION):
    patcher.apply_patch(project_dir, expected_revision=expected_revision, target_path=TARGET, patch_path=PATCH,
                        original_sha=ORIGINAL_SHA256, patched_sha=PATCHED_SHA256, patch_sha=PATCH_SHA256)


if 'Import' in globals():
    if env['PIOENV'].startswith('x4pro'):  # noqa: F821
        apply_patch(env['PROJECT_DIR'])  # noqa: F821
