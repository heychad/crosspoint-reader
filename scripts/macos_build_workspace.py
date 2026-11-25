#!/usr/bin/env python3
"""Run X4 Pro builds in one disposable 12 GiB macOS volume."""
import fcntl
import os
from pathlib import Path
import plistlib
import stat
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
GIT = ROOT / ".git"
STATE = GIT / "x4-build-space"
MOUNT = STATE / "mount"
IMAGE = STATE / "workspace.sparseimage"
OUTPUTS = (".pio", ".cache", "build")


def install():
    worktrees = subprocess.check_output(["git", "worktree", "list", "--porcelain"], cwd=ROOT)
    if [line for line in worktrees.splitlines() if line.startswith(b"worktree ")] != [
        b"worktree " + os.fsencode(ROOT)
    ]:
        raise ValueError("Consolidate existing worktrees before installing.")
    for name in OUTPUTS:
        link = ROOT / name
        expected = (MOUNT / name).relative_to(ROOT)
        if link.is_symlink() and link.readlink() == expected:
            continue
        if os.path.lexists(link):
            raise ValueError(f"Review and clear the existing {link} first.")
    guard = GIT / "worktrees"
    guard.mkdir(exist_ok=True)
    if any(guard.iterdir()):
        raise ValueError("The Git worktree registry must be empty.")
    os.chflags(guard, guard.stat().st_flags | stat.UF_IMMUTABLE)
    MOUNT.mkdir(mode=0o500, exist_ok=True)
    if os.path.ismount(MOUNT) or any(MOUNT.iterdir()):
        raise ValueError("The mount directory must be empty for setup.")
    MOUNT.chmod(0o500)
    for name in OUTPUTS:
        link = ROOT / name
        if not link.is_symlink():
            link.symlink_to((MOUNT / name).relative_to(ROOT), target_is_directory=True)


def discard():
    # Never remove an attached image or detach an unrelated disk.
    info = plistlib.loads(subprocess.check_output(["hdiutil", "info", "-plist"]))
    for disk in info["images"]:
        if Path(disk["image-path"]).resolve() != IMAGE:
            continue
        mounts = {Path(e["mount-point"]).resolve() for e in disk["system-entities"] if "mount-point" in e}
        if mounts != {MOUNT}:
            raise ValueError(f"Inspect the mounted image before cleanup: {IMAGE}")
        subprocess.run(["hdiutil", "detach", "-quiet", MOUNT], check=True)
    if os.path.ismount(MOUNT) or IMAGE.is_symlink():
        raise ValueError("Unexpected mount or image. Inspect the build workspace.")
    IMAGE.unlink(missing_ok=True)


def run(command, lock):
    for name in OUTPUTS:
        link = ROOT / name
        if not link.is_symlink() or link.readlink() != (MOUNT / name).relative_to(ROOT):
            raise ValueError("Run this script with --install first.")
    discard()
    try:
        subprocess.run(["hdiutil", "create", "-quiet", "-size", "12g", "-type", "SPARSE",
                        "-fs", "APFS", "-volname", "X4Build", IMAGE], check=True)
        subprocess.run(["hdiutil", "attach", "-quiet", "-nobrowse", "-mountpoint", MOUNT, IMAGE],
                       check=True)
        if not os.path.ismount(MOUNT) or MOUNT.stat().st_dev == ROOT.stat().st_dev:
            raise ValueError("The build volume did not mount.")
        for name in OUTPUTS:
            (MOUNT / name).mkdir()
        env = os.environ.copy()
        env["PATH"] = str(ROOT / ".venv/bin") + os.pathsep + env["PATH"]
        return subprocess.run(command, cwd=ROOT, env=env, pass_fds=(lock.fileno(),)).returncode
    finally:
        discard()


def main(args):
    if sys.platform != "darwin" or not GIT.is_dir():
        raise ValueError("Run from a primary macOS checkout.")
    if not args or args == ["--help"]:
        print("Usage: macos_build_workspace.py --install | --cleanup | -- COMMAND [ARGS...]")
        return 0 if args else 2
    STATE.mkdir(mode=0o700, exist_ok=True)
    with (STATE / "lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("A build session is already using the volume.") from None
        if args == ["--install"]:
            install()
        elif args == ["--cleanup"]:
            discard()
        elif args[0] == "--" and len(args) > 1:
            return run(args[1:], lock)
        else:
            raise ValueError("Use --install, --cleanup, or -- COMMAND.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
