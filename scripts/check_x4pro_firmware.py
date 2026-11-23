#!/usr/bin/env python3
"""Check the identity and size of an X4 Pro app image before packaging it."""

import argparse
import csv
import hashlib
from pathlib import Path
import re


def check_image(firmware: Path, version: str) -> str:
    partitions = Path(__file__).resolve().parents[1] / 'partitions.csv'
    with partitions.open(newline='', encoding='utf-8') as table:
        slots = {row[2].strip(): int(row[4], 0) for row in csv.reader(table)
                 if len(row) >= 5 and row[1].strip() == 'app' and row[2].strip() in ('ota_0', 'ota_1')}
    if len(slots) != 2:
        raise ValueError('Expected both X4 Pro OTA partitions')

    image = firmware.read_bytes()
    if not 24 <= len(image) <= min(slots.values()):
        raise ValueError('Firmware image size is outside the X4 Pro OTA slot bounds')
    if image[0] != 0xe9 or image[12:14] != b'\x09\x00':
        raise ValueError('Expected an ESP32-S3 app image')
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
