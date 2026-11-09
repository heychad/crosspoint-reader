"""Provide the pinned patched driver without modifying the SDK checkout."""
import hashlib
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('sdmmc_patcher', ROOT / 'scripts/patch_freeink_sdmmc.py')
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


def patched_source(config=patcher):
    source = (ROOT / 'freeink-sdk' / config.TARGET).read_bytes()
    digest = hashlib.sha256(source).hexdigest()
    if digest == config.PATCHED_SHA256:
        return source.decode()
    if digest != config.ORIGINAL_SHA256:
        raise RuntimeError('FreeInk SDMMC source contains unrecognized changes')
    patch = ROOT / config.PATCH
    if hashlib.sha256(patch.read_bytes()).hexdigest() != config.PATCH_SHA256:
        raise RuntimeError('FreeInk SDMMC patch hash mismatch')
    with tempfile.TemporaryDirectory() as tmp:
        target = Path(tmp) / config.TARGET
        target.parent.mkdir(parents=True)
        target.write_bytes(source)
        env = os.environ.copy()
        env.pop('GIT_INDEX_FILE', None)
        subprocess.run(['git', 'apply', str(patch)], cwd=tmp, env=env, check=True)
        result = target.read_bytes()
    if hashlib.sha256(result).hexdigest() != config.PATCHED_SHA256:
        raise RuntimeError('Unexpected patched FreeInk SDMMC source')
    return result.decode()
