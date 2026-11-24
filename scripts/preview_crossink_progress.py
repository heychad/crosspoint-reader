#!/usr/bin/env python3
"""Preview EPUB resume records from one backed-up CrossPoint or CrossInk cache."""
import argparse
import json
from pathlib import Path
import struct
import sys
import zlib


def decode(raw):
    """Decode the native record or the CrossInk RC3 ProgressRecord.h layout."""
    if len(raw) not in (4, 6, 10, 20):
        raise ValueError("unsupported record length")
    spine, page = struct.unpack_from("<HH", raw)
    count = struct.unpack_from("<H", raw, 4)[0] if len(raw) >= 6 else None
    offset = struct.unpack_from("<I", raw, 6)[0] if len(raw) >= 10 else None
    sequence = None
    if len(raw) == 20:
        marker, flags, sequence, checksum = struct.unpack_from("<BBII", raw, 10)
        if marker != 0xC5 or flags & ~1:
            raise ValueError("unrecognized marker or flags")
        if zlib.crc32(raw[:16]) != checksum:
            raise ValueError("checksum mismatch")
        if not flags & 1:
            offset = None
    # Keep the original page sentinel and the distinction between absent and zero offset.
    length = 10 if offset is not None else 6 if count is not None else 4
    return dict(format="native" if sequence is None else "rc3", spine=spine, page=page,
                page_count=count, text_offset=offset, sequence=sequence, native_hex=raw[:length].hex())


def read_slot(path):
    try:
        with path.open("rb") as source:
            raw = source.read(21)
    except FileNotFoundError:
        return {"status": "missing"}
    try:
        return {"status": "valid", **decode(raw)}
    except ValueError as error:
        return {"status": "invalid", "reason": str(error)}


def choose(primary, backup):
    """Return an inspectable choice only if firmware history cannot change the answer."""
    valid = [name for name, slot in (("primary", primary), ("backup", backup)) if slot["status"] == "valid"]
    if len(valid) < 2:
        return (valid[0], "only valid record") if valid else (None, "no valid record")
    if primary["native_hex"] == backup["native_hex"]:
        return "primary", "both positions match"
    if primary["format"] == "native":
        if backup["format"] == "native":
            return "primary", "native primary"
        return None, "native primary and RC3 backup differ; firmware history determines which is newer"
    if backup["format"] == "native":
        return "primary", "versioned primary"
    difference = (backup["sequence"] - primary["sequence"]) & 0xffffffff
    if difference in (0, 0x80000000):
        return None, "conflicting positions have tied or indeterminate sequence numbers"
    return ("backup" if difference < 0x80000000 else "primary"), "newer RC3 sequence"


def preview(directory):
    if directory.name.startswith("xtc_"):
        raise ValueError("XTC progress has a different format; select an EPUB cache")
    if not directory.is_dir():
        raise ValueError("expected one backed-up EPUB cache directory")
    primary = read_slot(directory / "progress.bin")
    backup = read_slot(directory / "progress.bin.bak")
    selected, reason = choose(primary, backup)
    return dict(primary=primary, backup=backup, selected=selected, reason=reason)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cache", type=Path, help="one extracted EPUB cache directory")
    args = parser.parse_args()
    try:
        result = preview(args.cache)
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0 if result["selected"] else 2


if __name__ == "__main__":
    sys.exit(main())
