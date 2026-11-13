"""Verify explicitly configured CA/hostname checks in the pinned FreeInk SDK."""
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
TARGET = Path('libs/network/SecureNet/src/SecureClient.cpp')
PATCH = Path('scripts/freeink_patches/0003-verified-tls-hostname.patch')
ORIGINAL_SHA256 = 'a3c8fb6cb2914cb2d6d550f2d2430622ea06841403a156a47c25ab59e17ef3e2'
PATCHED_SHA256 = 'cf014789a783bb0abc599e92077ed2268ce23d993ed339ffa036a7aaf69860a9'
PATCH_SHA256 = '9cafe9f6f7bcd244f919bd7507d60a6e18ce9f658a8df1c41a9b185c0b1e14f4'


def apply_patch(project_dir, expected_revision=patcher.SDK_REVISION):
    patcher.apply_patch(project_dir, expected_revision=expected_revision, target_path=TARGET, patch_path=PATCH,
                        original_sha=ORIGINAL_SHA256, patched_sha=PATCHED_SHA256, patch_sha=PATCH_SHA256)


if 'Import' in globals():
    if env['PIOENV'].startswith('x4pro'):  # noqa: F821
        apply_patch(env['PROJECT_DIR'])  # noqa: F821
