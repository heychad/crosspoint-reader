#!/usr/bin/env python3
"""Inspect, back up, or explicitly restore X4 Pro settings recovery files."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import tempfile

MAX_BYTES = 4 * 1024 * 1024
FIRMWARE_MAX_BYTES = 32768
NAMES = {'settings.json', 'settings.json.tmp', 'settings.json.bak'}
ARCHIVE = re.compile(r'settings\.recovered-\d\d\.bak\Z')


def read_directory(directory):
    mode = directory.lstat().st_mode
    if not stat.S_ISDIR(mode):
        raise ValueError('.crosspoint is not a directory')
    result = {}
    with os.scandir(directory) as entries:
        names = sorted(e.name for e in entries if e.name in NAMES or ARCHIVE.fullmatch(e.name))
    for name in names:
        path = directory / name
        flags = os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0)
        fd = os.open(path, flags)
        try:
            before = os.fstat(fd)
            if not stat.S_ISREG(before.st_mode) or before.st_size > MAX_BYTES:
                raise ValueError(f'{name}: not a supported settings file')
            chunks = []
            total = 0
            while True:
                chunk = os.read(fd, 4096)
                if not chunk:
                    break
                chunks.append(chunk)
                total += len(chunk)
                if total > MAX_BYTES:
                    raise ValueError(f'{name}: file is too large')
            after = os.fstat(fd)
            current = path.lstat()
            if (before.st_dev, before.st_ino, before.st_size, before.st_mtime_ns) != \
               (after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns) or \
               (before.st_dev, before.st_ino) != (current.st_dev, current.st_ino):
                raise ValueError(f'{name}: file changed during inspection')
            content = b''.join(chunks)
            try:
                def reject_constant(value):
                    raise ValueError(value)
                valid = isinstance(json.loads(content, parse_constant=reject_constant), dict)
            except (ValueError, UnicodeDecodeError, RecursionError):
                valid = False
            result[name] = {'bytes': content, 'sha256': hashlib.sha256(content).hexdigest(), 'json_object': valid}
        finally:
            os.close(fd)
    return result


def read_candidates(card):
    return read_directory(card / '.crosspoint')


def export(card, destination, candidates):
    target = destination.resolve(strict=False)
    volume = card.resolve(strict=True)
    if target == volume or volume in target.parents:
        raise ValueError('Backup destination must be outside the card')
    target.mkdir(mode=0o700, parents=False, exist_ok=False)
    for name, item in candidates.items():
        path = target / name
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(item['bytes'])
            stream.flush()
            os.fsync(stream.fileno())
        if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
            raise OSError(f'{name}: backup verification failed')
    if read_candidates(card) != candidates:
        raise OSError('Card files changed during export; keep the backup but inspect again')
    return target


def restore(card, backup, choice, apply=False):
    volume = card.resolve(strict=True)
    backup = backup.resolve(strict=True)
    if backup == volume or volume in backup.parents:
        raise ValueError('Backup must be outside the card')
    current = read_candidates(card)
    verified = read_directory(backup)
    if not current or current != verified:
        raise ValueError('Card files do not match the exported backup; inspect and export again')
    if choice not in current or not current[choice]['json_object'] or len(current[choice]['bytes']) > FIRMWARE_MAX_BYTES:
        raise ValueError('Choose a present JSON object within the firmware size limit')
    if not apply:
        return None

    folder = card / '.crosspoint'
    archive = Path(tempfile.mkdtemp(prefix='settings-recovery-', dir=folder))
    try:
        selected = archive / 'selected.copy'
        fd = os.open(selected, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(current[choice]['bytes'])
            stream.flush()
            os.fsync(stream.fileno())
        if selected.read_bytes() != current[choice]['bytes']:
            raise OSError('Could not verify the selected copy on the card')

        # Keep the original primary in place until the other active artifacts
        # have been archived. Never overwrite a file in the new directory.
        remaining = dict(current)
        for name in ('settings.json.tmp', 'settings.json.bak', 'settings.json'):
            if name not in remaining:
                continue
            if read_candidates(card) != remaining:
                raise OSError('Card files changed during restore')
            destination = archive / name
            if destination.exists():
                raise OSError('Recovery archive collision')
            os.rename(folder / name, destination)
            remaining.pop(name)

        if read_candidates(card) != remaining:
            raise OSError('Card files changed during restore')
        primary = folder / 'settings.json'
        fd = os.open(primary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as stream:
            stream.write(current[choice]['bytes'])
            stream.flush()
            os.fsync(stream.fileno())
        if primary.read_bytes() != current[choice]['bytes']:
            raise OSError('Restored settings did not verify')
        return archive
    except (Exception, KeyboardInterrupt) as error:
        raise OSError(f'Restore interrupted; keep the card mounted. Backup: {backup}; card archive: {archive}') from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('card', type=Path, help='Mounted X4 Pro card path')
    parser.add_argument('--export', type=Path, metavar='NEW_DIRECTORY', help='Save private verified copies outside the card')
    parser.add_argument('--restore-from', type=Path, metavar='BACKUP_DIRECTORY', help='Use a matching exported backup')
    parser.add_argument('--choose', metavar='FILENAME', help='Settings copy to restore')
    parser.add_argument('--apply', action='store_true', help='Write the selected copy to the card')
    args = parser.parse_args()
    if args.export and (args.restore_from or args.choose or args.apply):
        parser.error('--export cannot be combined with restore options')
    if bool(args.restore_from) != bool(args.choose) or (args.apply and not args.restore_from):
        parser.error('--restore-from and --choose are required together; --apply writes to the card')
    card = args.card.resolve(strict=True)
    candidates = read_candidates(card)
    if not candidates:
        raise SystemExit('No settings recovery files found.')
    for name, item in candidates.items():
        supported = len(item['bytes']) <= FIRMWARE_MAX_BYTES
        print(f"{name}: {len(item['bytes'])} bytes, sha256 {item['sha256']}, JSON object: {'yes' if item['json_object'] else 'no'}, within firmware size limit: {'yes' if supported else 'no'}")
    if args.export:
        print(f'Verified copies saved to {export(card, args.export, candidates)}')
    if args.restore_from:
        archive = restore(card, args.restore_from, args.choose, args.apply)
        if archive:
            print(f'Restored {args.choose}; original files are preserved in {archive}')
        else:
            print(f'Ready to restore {args.choose}. Add --apply to write to the card.')


if __name__ == '__main__':
    main()
