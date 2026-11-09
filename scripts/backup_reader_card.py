#!/usr/bin/env python3
"""Copy and verify reader state from a mounted, inactive X4 Pro card."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat

STATE_DIRS = ('.crosspoint', '.crossink-stats-backup')


def inventory(root):
    root = Path(root)
    result = {}
    for name in STATE_DIRS:
        base = root / name
        try:
            mode = base.lstat().st_mode
        except FileNotFoundError:
            if name == '.crosspoint':
                raise ValueError('The .crosspoint state folder is missing.')
            continue
        if not stat.S_ISDIR(mode):
            raise ValueError(f'Invalid state folder: {name}')
        result[name + '/'] = 'directory'
        def fail(error):
            raise error
        for parent, dirs, files in os.walk(base, followlinks=False, onerror=fail):
            for child in dirs + files:
                path = Path(parent) / child
                mode = path.lstat().st_mode
                rel = path.relative_to(root).as_posix()
                if stat.S_ISDIR(mode):
                    result[rel + '/'] = 'directory'
                elif stat.S_ISREG(mode):
                    with path.open('rb') as stream:
                        result[rel] = hashlib.file_digest(stream, 'sha256').hexdigest()
                else:
                    raise ValueError(f'Unsupported state item: {rel}')
    return dict(sorted(result.items()))


def backup(reader, destination):
    reader = Path(reader).resolve(strict=True)
    destination = Path(destination)
    parent = destination.parent.resolve(strict=True)
    destination = parent / destination.name
    if not reader.is_dir() or destination == reader or destination.is_relative_to(reader) or reader.is_relative_to(destination):
        raise ValueError('Backup must be a new folder outside the reader.')
    before = inventory(reader)
    destination.mkdir(mode=0o700)
    for name in STATE_DIRS:
        if name + '/' in before:
            shutil.copytree(reader / name, destination / name, symlinks=True)
    if inventory(destination) != before or inventory(reader) != before:
        raise ValueError('State changed or backup verification failed; inspect the incomplete backup.')
    receipt = {'state_sha256': before, 'reader': str(reader),
               'note': 'Matching inventories do not prove device identity or an atomic snapshot.'}
    with (destination / 'backup-receipt.json').open('x', encoding='utf-8') as output:
        json.dump(receipt, output, indent=2, ensure_ascii=False)
        output.write('\n')
    return sum(value != 'directory' for value in before.values())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('reader', help='Mounted reader card, confirmed by the user')
    parser.add_argument('destination', help='New private backup folder outside the reader')
    args = parser.parse_args()
    print(f'Verified {backup(args.reader, args.destination)} state files in {args.destination}')


if __name__ == '__main__':
    main()
