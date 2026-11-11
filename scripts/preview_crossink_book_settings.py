#!/usr/bin/env python3
"""Inspect a CrossInk per-book settings backup without changing it."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

# The order and version gates follow EpubReaderActivity::readReaderSettingsSnapshot
# through CrossInk commit 1990a78f2cbe015ecda91030fe85aca30624d89c.
MASK_FIELDS = (
    'fontFamily', 'readerFontPointSize', 'lineHeightPercent', 'wordSpacing',
    'orientation', 'screenMarginVertical', 'screenMarginHorizontal',
    'publisherPageNumbers', 'paragraphAlignment', 'embeddedStyle',
    'hyphenationEnabled', 'textAntiAliasing', 'imageRendering',
    'extraParagraphSpacing', 'forceParagraphIndents', 'focusReadingEnabled',
    'guideReadingEnabled', 'indexingMethod', 'sdFontFamilyName',
)
COMPATIBLE = {
    # CrossInk treats values below 8 as legacy font-size slots, not points.
    'readerFontPointSize': ('fontSize', 8, 255),
    'orientation': ('orientation', 0, 3),
    'paragraphAlignment': ('paragraphAlignment', 0, 4),
    'embeddedStyle': ('embeddedStyle', 0, 1),
    'hyphenationEnabled': ('hyphenationEnabled', 0, 1),
    'textAntiAliasing': ('textAntiAliasing', 0, 1),
    'imageRendering': ('imageRendering', 0, 2),
    'extraParagraphSpacing': ('extraParagraphSpacing', 0, 1),
    'focusReadingEnabled': ('focusReadingEnabled', 0, 1),
}
LINE_HEIGHTS = (100, 110, 120, 130, 145, 160, 175)

class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, count: int) -> bytes:
        end = self.pos + count
        if end > len(self.data):
            raise ValueError('truncated settings record')
        value = self.data[self.pos:end]
        self.pos = end
        return value

    def u8(self) -> int:
        return self.take(1)[0]

    def integer(self, count: int) -> int:
        return int.from_bytes(self.take(count), 'little')

    def name(self) -> str:
        raw = self.take(64)
        if b'\0' not in raw:
            raise ValueError('font name is not terminated')
        try:
            return raw.split(b'\0', 1)[0].decode('utf-8')
        except UnicodeDecodeError as error:
            raise ValueError('font name is not UTF-8') from error


def decode(data: bytes) -> dict:
    if not data or len(data) > 256:
        raise ValueError('invalid settings record size')
    reader = Reader(data)
    version = reader.u8()
    if not 1 <= version <= 13:
        raise ValueError('unsupported settings record version')
    if version == 1:
        seconds = reader.integer(2)
        if reader.pos != len(data):
            raise ValueError('unexpected trailing data')
        return {'version': 1, 'autoPageTurnSeconds': seconds, 'overrides': {}, 'preview': {},
                'unmapped': ['autoPageTurnSeconds'] if seconds else []}
    flags, seconds, render_mode = reader.u8(), reader.integer(2), reader.u8()
    if flags & ~0x1f:
        raise ValueError('unknown settings flags')
    snap = {}
    for key in ('fontFamily', 'readerFontPointSize', 'lineHeightPercent'):
        snap[key] = reader.u8()
    if version >= 3:
        snap['wordSpacing'] = reader.u8()
    snap['orientation'] = reader.u8()
    if version >= 8:
        snap['screenMarginVertical'], snap['screenMarginHorizontal'] = reader.u8(), reader.u8()
    else:
        snap['screenMarginVertical'] = snap['screenMarginHorizontal'] = reader.u8()
    for key in ('publisherPageNumbers', 'paragraphAlignment', 'embeddedStyle', 'hyphenationEnabled',
                'textAntiAliasing'):
        snap[key] = reader.u8()
    if version <= 8:
        reader.u8()  # Obsolete per-book dark mode; CrossInk ignores it too.
    for key in ('imageRendering', 'extraParagraphSpacing', 'forceParagraphIndents',
                'focusReadingEnabled', 'guideReadingEnabled', 'epubRenderMode'):
        snap[key] = reader.u8()
    if version >= 4:
        snap['indexingMethod'] = reader.u8()
    snap['sdFontFamilyName'] = reader.name()
    dictionary = reader.name() if version >= 5 else None
    dictionary_size = reader.u8() if version >= 7 else None
    fields = MASK_FIELDS
    if version >= 11:
        fields = MASK_FIELDS[:18] + ('imageGrayscale', 'sdFontFamilyName')
    if version >= 12:
        fields += ('characterSpacing',)
    if version >= 13:
        fields += ('publisherFonts',)
    mask = reader.integer(4) if version >= 10 else (1 << len(fields)) - 1
    if mask & ~((1 << len(fields)) - 1):
        raise ValueError('unknown override bits')
    for key, maximum in (('imageGrayscale', 1), ('characterSpacing', 10), ('publisherFonts', 2)):
        minimum_version = {'imageGrayscale': 11, 'characterSpacing': 12, 'publisherFonts': 13}[key]
        if version >= minimum_version:
            snap[key] = reader.u8()
            if snap[key] > maximum:
                raise ValueError('invalid ' + key)
    if reader.pos != len(data):
        raise ValueError('unexpected trailing data')
    overrides = {key: snap[key] for bit, key in enumerate(fields)
                 if flags & 1 and mask & (1 << bit) and key in snap}
    preview, unmapped = {}, []
    image_pair = (version >= 11 and overrides.get('imageRendering') == 0
                  and 'imageGrayscale' in overrides)
    for key, value in overrides.items():
        if image_pair and key in ('imageRendering', 'imageGrayscale'):
            preview['imageRendering'] = 0 if overrides['imageGrayscale'] else 3
            continue
        if version >= 11 and key == 'imageRendering' and value == 0:
            unmapped.append(key)
            continue
        if key == 'characterSpacing' and 1 <= value <= 9 and value % 2:
            preview['characterSpacing'] = (value - 1) // 2
            continue
        if key == 'lineHeightPercent' and value in LINE_HEIGHTS:
            preview['lineHeight'] = LINE_HEIGHTS.index(value)
            continue
        if key == 'wordSpacing' and value == 0:
            preview['wordSpacing'] = 100
            continue
        # One fork margin replaces two CrossInk margins only when both were
        # explicit, equal and within the native setting's supported range.
        if key in ('screenMarginVertical', 'screenMarginHorizontal'):
            if (overrides.get('screenMarginVertical') == overrides.get('screenMarginHorizontal')
                    and overrides.get('screenMarginVertical') is not None and 5 <= value <= 40):
                preview['screenMargin'] = value
                continue
        spec = COMPATIBLE.get(key)
        if spec and spec[1] <= value <= spec[2]:
            preview[spec[0]] = value
        else:
            unmapped.append(key)
    if flags & 2 and seconds:
        unmapped.append('autoPageTurnSeconds')
    if flags & 4:
        unmapped.append('renderMode')
    if flags & 8:
        unmapped.append('dictionaryFont')
    if flags & 16:
        unmapped.append('safeMode')
    return {'version': version, 'flags': flags, 'autoPageTurnSeconds': seconds,
            'renderMode': render_mode, 'dictionaryFont': dictionary,
            'dictionaryFontPointSize': dictionary_size, 'overrides': overrides,
            'preview': preview, 'firmware_import_excluded': ['fontSize'] if 'fontSize' in preview else [],
            'unmapped': unmapped}


def compare_current(report: dict, current: dict | None) -> dict:
    """Compare fields without proposing changes to an existing native file."""
    if report.get('flags', 0) & 0x10:
        return {'status': 'safe_mode_requires_review', 'fields': {}, 'patch_for_missing_fields_only': {}}
    if current is None:
        return {'status': 'needs_current_settings', 'fields': {}, 'patch_for_missing_fields_only': {}}
    if (not isinstance(current, dict) or type(current.get('version')) is not int
            or current['version'] != 1 or not isinstance(current.get('overrides'), dict)):
        raise ValueError('current settings must have version 1 and an overrides object')
    existing = current['overrides']
    fields = {}
    for key, value in report['preview'].items():
        if key not in existing:
            fields[key] = 'missing_requires_manual_review'
        elif type(existing[key]) is int and existing[key] == value:
            fields[key] = 'already_matching'
        else:
            fields[key] = 'existing_value_preserved'
    return {'status': 'native_file_takes_precedence', 'fields': fields, 'patch_for_missing_fields_only': {}}


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('settings', type=Path, help='local backup of reader_settings.bin')
    parser.add_argument('--current', type=Path, help='local copy of the current reader_prefs.json')
    args = parser.parse_args()
    try:
        with args.settings.open('rb') as stream:
            data = stream.read(257)
        report = decode(data)
        current = None
        if args.current:
            with args.current.open('rb') as stream:
                raw = stream.read(4097)
            if len(raw) > 4096:
                raise ValueError('current settings exceed 4096 bytes')
            def reject_constant(_value):
                raise ValueError('invalid JSON constant')
            current = json.loads(raw, object_pairs_hook=unique_object, parse_constant=reject_constant)
            if not isinstance(current, dict):
                raise ValueError('current settings must be a JSON object')
        report['comparison'] = compare_current(report, current)
    except (OSError, ValueError) as error:
        print(f'Cannot inspect settings: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'notice': 'Decoder candidates only; excluded fields are not imported by firmware. '
                                'No files or reader settings were changed.', **report}, indent=2))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
