"""
PlatformIO post: script: apply CrossPoint's SdFat patches via `git apply`.

The patches in `scripts/sdfat_patches/` make USE_SEPARATE_FAT_CACHE
overridable, invalidate the sector cache after a failed read, and write the
FAT cache before the data cache on sync, preserve LFN lookup read errors, and
provide scoped FAT and exFAT directory checks (see the patches). They target
SdFat 2.3.1 only.

Unlike JPEGDEC, SdFat is a registry dependency of the SDK, so the script
patches only the copy that the current environment actually builds:
  * the target must live under this project's `.pio/libdeps/<env>/`
  * `library.properties` must say version 2.3.1
  * each target file must hash to the reviewed upstream or patched bytes
Any mismatch fails the build instead of compiling an unreviewed cache.
Upgrading SdFat therefore requires re-reviewing the patches and hashes.

All patches are checked before any file is changed, and already-patched
files are left untouched so incremental builds do not recompile SdFat.
"""

import hashlib
import os
from pathlib import Path
import subprocess


# Reviewed upstream and patched bytes. A dependency upgrade requires re-review.
PATCHES = (
    ("0001-invalidate-failed-cache-fill.patch", "src/common/FsCache.cpp",
     "ba4f99dd660c7c6a747b20abcf11354379aa689115d1ee1ad1cf9577706a67bb",
     "f46a551f97c674ab00c9c25385af7c370a776725f17e14502fcc357858f583da"),
    ("0002-allow-separate-fat-cache-override.patch", "src/SdFatConfig.h",
     "7889975cad262158e1623c873730210c41c55c2198830d668952b82e588ff7cc",
     "32104db82acc857b70c7fe20740afbffa9f7dee0a7c685c106febfa989fdbe77"),
    ("0003-sync-fat-before-data-cache.patch", "src/FatLib/FatPartition.h",
     "fb91c81beac2779a0d4b08e886a5fefdf8062b8ad422ccec4e178ae4e88b72e5",
     "10d66a5d2424ed9c6919a9dc65305ebd5854170bdeea29f4b7c70f57bee929af"),
    ("0004-preserve-lfn-read-errors.patch", "src/FatLib/FatFileLFN.cpp",
     "e7a13328625505c5f2c18279087a36d90b308bc7c97513371a6ce14884026be2",
     "e5506cfd44e2776482404d3e378a458538aa9debd3d8a2640c42728b56f846ac"),
    ("0007-fatfile-strict-lookup.patch", "src/FatLib/FatFile.h",
     "42355cd5bf5dc6e37b13931922e029a7c2a64bc409e7b3e56b632021ee925644",
     "dc07c47d649314aa058955a0564e0e29ff42d233f08a58f0f4a0b4e07387f7bd"),
    ("0008-fsfile-strict-lookup.patch", "src/FsLib/FsFile.h",
     "4c77e48772c707876f56c48ab71dbfb0b66f4de12375ead237c8d929d7755126",
     "36c47655281a70a46eb2e4717d5c6d399bacf9522b7a4aa2c8fa3218facbbd06"),
    ("0010-exfat-strict-header.patch", "src/ExFatLib/ExFatFile.h",
     "a0863afa1035a8dd799e8d17899a62369f9b50b3916a197ce99504ba3cb8ab13",
     "9fb9de05d16ccb2e71137a2a438bed927bc7dd1a224521980270b6b7203ec188"),
    ("0011-exfat-strict-scan.patch", "src/ExFatLib/ExFatFile.cpp",
     "ebcdb4c15e9200caf15a93095a37428d987e96da0f85733104550373ce6f0aa0",
     "2a0e7b2dbc716d89c2ee9c54b0658ef79b0757fc824b7d4a3adc55a933bdf4ec"),
    ("0017-fat-source-bounded-chain.patch", "src/FatLib/FatFile.cpp",
     "c1757a57ea134df2aab650d2db109b7d731f66cf309e5a070a9b50667c258948",
     "cbe7fc25f8a5cdf424697804bfd4ed170027d0f21543bdc1a9dfd7fd89aae457"),
)

MIGRATIONS = {
    "src/FatLib/FatFileLFN.cpp": {
        "7aa833b48168d6f102babd0f0804fc3a6206c60e211ccf117f12ff3273992c84":
            "0005-upgrade-lfn-create-guard.patch",
        "69da34b4283eb63603404f0c3ecfa3d73ab8210cba9d341596cd1750a3c19919":
            "0006-enable-strict-fat-lookup.patch",
    },
    "src/FatLib/FatFile.h": {
        "ec69d17ea1acae4b153099f3c4f5d142a536e41987a8872c09eb8378667724f8":
            "0013-fatfile-bounded-chain.patch",
    },
    "src/FsLib/FsFile.h": {
        "e72a825f134494e240ee85f3406949dd45e5562de7de9ff0afa2f84d536897b6":
            "0018-fsfile-prior-bounded-chain.patch",
        "1763b25f564226de9886e25efefa4b6d74bb6280550b1d891d6d752caa9305e6":
            "0014-fsfile-bounded-chain.patch",
    },
    "src/ExFatLib/ExFatFile.h": {
        "a6e2641cf08e28c3ea03b8d56d0d14633bfacebef2844c7d9189caad9f33cccc":
            "0015-exfat-header-bounded-chain.patch",
    },
    "src/ExFatLib/ExFatFile.cpp": {
        "927452f417c657b0c6ec593b805e72b5121080d6719ebd1650b3941a30602703":
            "0019-exfat-prior-bounded-chain.patch",
        "37e029d11544dacdd13790fead896fe5a246190e7a93c7b24426fdffc7998fdd":
            "0016-exfat-source-bounded-chain.patch",
    },
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def is_within(path, root):
    # Path.is_relative_to() needs Python 3.9; the documented minimum is 3.8.
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def apply_patches(project_dir, dependency_dir):
    project = Path(project_dir).resolve()
    dependency = Path(dependency_dir).resolve()
    local_root = project / ".pio" / "libdeps"
    if not is_within(dependency, local_root):
        raise RuntimeError("SdFat must be inside this project's .pio/libdeps")
    properties = dependency / "library.properties"
    targets = [properties] + [dependency / patch[1] for patch in PATCHES]
    for target in targets:
        if not is_within(target.resolve(), dependency) or not target.is_file():
            raise RuntimeError("Missing or escaping SdFat patch target: " + target.name)
    if "version=2.3.1" not in properties.read_text().splitlines():
        raise RuntimeError("SdFat patches require version 2.3.1")

    # Archive dependencies must not discover the enclosing application Git repo.
    process_env = {key: os.environ[key] for key in ("HOME", "PATH", "TMPDIR", "LANG") if key in os.environ}
    process_env.update(GIT_CEILING_DIRECTORIES=str(dependency.parent),
                       GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    pending = []
    for name, relative, upstream, patched in PATCHES:
        target = dependency / relative
        current = digest(target)
        migrations = MIGRATIONS.get(relative, {})
        if current not in (upstream, patched) and current not in migrations:
            raise RuntimeError("Unrecognized SdFat source: " + relative)
        patch_name = migrations.get(current, name)
        patch = project / "scripts" / "sdfat_patches" / patch_name
        # Validate the entire set before changing any dependency files.
        command = ["git", "apply", "--check"]
        if current == patched:
            command.append("--reverse")
        result = subprocess.run(command + [str(patch)], cwd=dependency,
                                env=process_env, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError("SdFat patch does not apply: " + name + "\n" + result.stderr)
        if current != patched:
            pending.append((patch, target, patched))
    for patch, target, expected in pending:
        subprocess.run(["git", "apply", str(patch)], cwd=dependency, env=process_env, check=True)
        if digest(target) != expected:
            raise RuntimeError("Unexpected patched SdFat source: " + target.name)
        print("Applied SdFat patch: " + patch.name)


def patch_selected_dependency(env):
    # PioArduino's SDK-only pass clears lib_deps; the later app pass resolves them.
    if env.get("ARDUINO_LIB_COMPILE_FLAG") == "Build":
        return
    # A post: script sees the resolved library builders on the first build too.
    selected = [builder for builder in env.GetLibBuilders()
                if builder.name == "SdFat" and builder.is_dependent]
    if len(selected) != 1:
        raise RuntimeError("Expected exactly one selected SdFat dependency")
    dependency = Path(selected[0].path).resolve()
    environment_root = Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env["PIOENV"]
    if not is_within(dependency, environment_root.resolve()):
        raise RuntimeError("SdFat is outside the selected environment's dependencies")
    apply_patches(env["PROJECT_DIR"], dependency)


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    patch_selected_dependency(env)  # noqa: F821
