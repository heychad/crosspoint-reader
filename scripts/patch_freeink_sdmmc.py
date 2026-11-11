"""Apply the hash-pinned X4 Pro SDMMC I/O monitor to the local FreeInk SDK."""
import hashlib
import os
from pathlib import Path
import subprocess

SDK_REVISION = '6a208c0c46cce99bafa334bc4f171f5e558c5195'
TARGET = Path('libs/hardware/SDCardManager/src/SdmmcBlockDevice.cpp')
PATCH = Path('scripts/freeink_patches/0001-x4pro-sync-io-monitor.patch')
ORIGINAL_SHA256 = 'db88b2009b85e7fcffcf53a2b355d92c32178b743dd9fb4f08eb93ee05eebba5'
PATCHED_SHA256 = 'ac769da4210d39c21708ea4ce3a26f252ed522d0bdd7a2f07aaf40034fdd9201'
PATCH_SHA256 = '734a9191a1ab91bfb875aefed67c56325f621c38f481fb9b15682394217a6cb8'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def within(path, root):
    try:
        path.relative_to(root)
    except ValueError:
        return False
    return True


def apply_patch(project_dir, expected_revision=SDK_REVISION, *, target_path=TARGET, patch_path=PATCH,
                original_sha=ORIGINAL_SHA256, patched_sha=PATCHED_SHA256, patch_sha=PATCH_SHA256):
    project = Path(project_dir).resolve()
    sdk_path = project / 'freeink-sdk'
    sdk = sdk_path.resolve()
    target = sdk / target_path
    patch = project / patch_path
    if sdk_path.is_symlink() or not within(sdk, project):
        raise RuntimeError('FreeInk SDK must be inside this checkout')
    if not target.is_file() or not within(target.resolve(), sdk) or target.is_symlink():
        raise RuntimeError('Missing or escaping FreeInk SDMMC source')
    if not patch.is_file() or patch.is_symlink() or not within(patch.resolve(), project) or digest(patch) != patch_sha:
        raise RuntimeError('Missing or altered FreeInk SDMMC patch')
    process_env = os.environ.copy()
    process_env.pop('GIT_INDEX_FILE', None)
    root = subprocess.run(['git', 'rev-parse', '--show-toplevel'], cwd=sdk, env=process_env,
                          capture_output=True, text=True, check=True).stdout.strip()
    if Path(root).resolve() != sdk:
        raise RuntimeError('FreeInk SDK is not a separate checkout')
    rev = subprocess.run(['git', 'rev-parse', 'HEAD'], cwd=sdk, env=process_env,
                         capture_output=True, text=True, check=True).stdout.strip()
    if rev != expected_revision:
        raise RuntimeError('FreeInk SDK revision does not match the pinned revision')
    current = digest(target)
    if current not in (original_sha, patched_sha):
        raise RuntimeError('FreeInk SDMMC source contains unrecognized changes')
    command = ['git', 'apply', '--check']
    if current == patched_sha:
        command.append('--reverse')
    subprocess.run(command + [str(patch)], cwd=sdk, env=process_env, check=True, capture_output=True)
    if current == original_sha:
        subprocess.run(['git', 'apply', str(patch)], cwd=sdk, env=process_env, check=True)
        if digest(target) != patched_sha:
            raise RuntimeError('Unexpected patched FreeInk SDMMC source')


if 'Import' in globals():
    Import('env')  # noqa: F821 -- supplied by SCons
    if env['PIOENV'].startswith('x4pro'):  # noqa: F821
        apply_patch(env['PROJECT_DIR'])  # noqa: F821
