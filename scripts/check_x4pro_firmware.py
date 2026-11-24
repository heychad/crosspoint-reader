#!/usr/bin/env python3
"""Check the identity and size of an X4 Pro app image before packaging it."""

import argparse
import csv
from functools import reduce
import hashlib
from operator import xor
from pathlib import Path
import re


def verify_image_integrity(image: bytes) -> None:
    """Check the ESP app segments and footer using the SD updater's file layout."""
    position = 24
    checksum = 0xef
    data = memoryview(image)
    for _ in range(image[1]):
        if position + 8 > len(image):
            raise ValueError('Incomplete ESP segment header')
        length = int.from_bytes(data[position + 4:position + 8], 'little')
        position += 8
        end = position + length
        if end > len(image):
            raise ValueError('Incomplete ESP segment data')
        checksum = reduce(xor, data[position:end], checksum)
        position = end

    # The checksum occupies the last byte of the padded 16-byte block.
    footer_end = (position + 16) & ~15
    has_digest = image[23] != 0
    if footer_end + (32 if has_digest else 0) != len(image):
        raise ValueError('ESP image footer has the wrong size')
    if image[footer_end - 1] != checksum:
        raise ValueError('ESP segment checksum mismatch')
    if has_digest and hashlib.sha256(data[:footer_end]).digest() != data[footer_end:]:
        raise ValueError('ESP image validation hash mismatch')


def check_image(firmware: Path, version: str) -> str:
    partitions = Path(__file__).resolve().parents[1] / 'partitions.csv'
    with partitions.open(newline='', encoding='utf-8') as table:
        slots = {row[2].strip(): int(row[4], 0) for row in csv.reader(table)
                 if len(row) >= 5 and row[1].strip() == 'app' and row[2].strip() in ('ota_0', 'ota_1')}
    if len(slots) != 2:
        raise ValueError('Expected both X4 Pro OTA partitions')

    limit = min(slots.values())
    with firmware.open('rb') as source:
        image = source.read(limit + 1)
    if not 64 * 1024 <= len(image) <= limit:
        raise ValueError('Firmware image size is outside the X4 Pro OTA slot bounds')
    if image[0] != 0xe9 or image[12:14] != b'\x09\x00':
        raise ValueError('Expected an ESP32-S3 app image')
    verify_image_integrity(image)
    boards = re.findall(rb'CROSSPOINT-BOARD-V1:([!-:<-~]{1,23});', image)
    expected_version = rb'(?<![A-Za-z0-9.+-])' + re.escape(version.encode('ascii')) + rb'\x00'
    if boards != [b'x4pro'] or re.search(expected_version, image) is None:
        raise ValueError('Firmware board tag or embedded version does not match the requested build')
    return hashlib.sha256(image).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('firmware', type=Path)
    parser.add_argument('version', help='exact version shown by the reader')
    args = parser.parse_args()
    try:
        print(check_image(args.firmware, args.version))
    except (OSError, ValueError) as error:
        parser.exit(1, f'{error}\n')


if __name__ == '__main__':
    main()
