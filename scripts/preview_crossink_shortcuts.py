#!/usr/bin/env python3
"""Preview X4 Pro shortcut migration from CrossInk 1.6.1; never write settings."""
from __future__ import annotations

import argparse
from enum import IntEnum
import json
from pathlib import Path
import sys

MAX_BYTES = 1024 * 1024


class ReaderAction(IntEnum):
    DISABLED = 1
    PREVIOUS_PAGE = 2
    NEXT_PAGE = 3
    PREVIOUS_CHAPTER = 4
    NEXT_CHAPTER = 5
    BOOKMARK = 7
    DICTIONARY = 8
    FOOTNOTES = 9
    REFRESH = 10
    TOGGLE_FRONTLIGHT = 11
    ROTATE = 14
    ROTATE_BACKWARD = 15
    LARGER_TEXT = 18
    SMALLER_TEXT = 19
    ROTATE_HALF_TURN = 20
    QUICK_ACTIONS = 21
    FILE_BROWSER = 23
    LIBRARY = 24
    READING_STATISTICS = 25
    SCREENSHOT = 26
    SYNC_PROGRESS = 27
    CREATE_CLIPPING = 28
    TOGGLE_NIGHT = 29
    TOGGLE_FOCUS = 30
    TOGGLE_TOUCH = 31
    TOGGLE_TILT = 32
    QUICK_LOCK = 34
    SLEEP = 35
    FILE_TRANSFER = 37
    CYCLE_FONT_FAMILY = 39
    TOGGLE_HOME_BUTTON = 40
    TOGGLE_COMPLETION = 41
    HOME_READER = 42


class HomeAction(IntEnum):
    HOME = 0
    IGNORE = 1
    NEXT_PAGE = 2
    REFRESH = 3
    FOOTNOTES = 4
    SYNC_PROGRESS = 6
    BOOKMARK = 7
    DICTIONARY = 8
    READER_MENU = 9
    TOGGLE_FRONTLIGHT = 10
    CREATE_CLIPPING = 11
    QUICK_ACTIONS = 12
    QUICK_LOCK = 13
    FILE_BROWSER = 14
    LIBRARY = 15
    FILE_TRANSFER = 17
    READING_STATISTICS = 18
    PREVIOUS_PAGE = 19
    SCREENSHOT = 20
    TOGGLE_NIGHT = 21
    TOGGLE_TOUCH = 22
    TOGGLE_TILT = 23
    SLEEP = 24
    TOGGLE_COMPLETION = 25
    CYCLE_FONT_FAMILY = 26


# CrossInk v1.6.1 SHORT_PWRBTN and SIDE_BUTTON_ACTION values, not display indices.
SIDE_ACTIONS = {
    0: ReaderAction.DISABLED, 1: ReaderAction.SLEEP, 2: ReaderAction.NEXT_PAGE,
    3: ReaderAction.REFRESH, 4: ReaderAction.CYCLE_FONT_FAMILY, 6: ReaderAction.TOGGLE_FOCUS, 7: ReaderAction.BOOKMARK, 8: ReaderAction.SYNC_PROGRESS, 9: ReaderAction.TOGGLE_COMPLETION, 10: ReaderAction.READING_STATISTICS,
    11: ReaderAction.SCREENSHOT, 13: ReaderAction.FILE_TRANSFER, 14: ReaderAction.TOGGLE_TILT, 15: ReaderAction.TOGGLE_NIGHT,
    16: ReaderAction.FOOTNOTES, 17: ReaderAction.FILE_BROWSER, 21: ReaderAction.CREATE_CLIPPING, 22: ReaderAction.DICTIONARY,
    26: ReaderAction.TOGGLE_HOME_BUTTON,
    27: ReaderAction.QUICK_ACTIONS, 28: ReaderAction.TOGGLE_FRONTLIGHT, 29: ReaderAction.TOGGLE_TOUCH,
    30: ReaderAction.QUICK_LOCK, 31: ReaderAction.PREVIOUS_PAGE, 33: ReaderAction.LIBRARY, 36: ReaderAction.HOME_READER,
    64: ReaderAction.PREVIOUS_CHAPTER, 65: ReaderAction.NEXT_CHAPTER,
    66: ReaderAction.LARGER_TEXT, 67: ReaderAction.SMALLER_TEXT,
    68: ReaderAction.ROTATE_BACKWARD, 69: ReaderAction.ROTATE, 70: ReaderAction.ROTATE_HALF_TURN,
}
# CrossInk uses a different POWER_CHORD_ACTION enum for the side-button chord.
CHORD_ACTIONS = {
    0: ReaderAction.SCREENSHOT, 1: ReaderAction.QUICK_LOCK, 4: ReaderAction.DISABLED,
    5: ReaderAction.SLEEP, 6: ReaderAction.NEXT_PAGE, 7: ReaderAction.BOOKMARK, 8: ReaderAction.READING_STATISTICS,
    9: ReaderAction.TOGGLE_COMPLETION, 10: ReaderAction.REFRESH, 11: ReaderAction.CYCLE_FONT_FAMILY, 13: ReaderAction.TOGGLE_FOCUS, 15: ReaderAction.SYNC_PROGRESS, 16: ReaderAction.FILE_TRANSFER,
    20: ReaderAction.TOGGLE_NIGHT, 21: ReaderAction.FOOTNOTES, 22: ReaderAction.FILE_BROWSER, 23: ReaderAction.CREATE_CLIPPING,
    24: ReaderAction.DICTIONARY, 25: ReaderAction.TOGGLE_HOME_BUTTON, 26: ReaderAction.QUICK_ACTIONS, 27: ReaderAction.TOGGLE_FRONTLIGHT,
    28: ReaderAction.TOGGLE_TOUCH, 29: ReaderAction.PREVIOUS_PAGE, 31: ReaderAction.LIBRARY, 32: ReaderAction.HOME_READER,
}
# CrossInk Home uses SHORT_PWRBTN plus dedicated raw values 23-25.
HOME_ACTIONS = {
    0: HomeAction.IGNORE, 1: HomeAction.SLEEP, 2: HomeAction.NEXT_PAGE, 3: HomeAction.REFRESH, 4: HomeAction.CYCLE_FONT_FAMILY,
    7: HomeAction.BOOKMARK, 8: HomeAction.SYNC_PROGRESS, 9: HomeAction.TOGGLE_COMPLETION, 10: HomeAction.READING_STATISTICS,
    11: HomeAction.SCREENSHOT, 13: HomeAction.FILE_TRANSFER, 14: HomeAction.TOGGLE_TILT,
    15: HomeAction.TOGGLE_NIGHT, 16: HomeAction.FOOTNOTES, 17: HomeAction.FILE_BROWSER, 21: HomeAction.CREATE_CLIPPING, 22: HomeAction.DICTIONARY,
    23: HomeAction.HOME, 24: HomeAction.TOGGLE_FRONTLIGHT, 25: HomeAction.READER_MENU,
    27: HomeAction.QUICK_ACTIONS, 28: HomeAction.TOGGLE_FRONTLIGHT, 29: HomeAction.TOGGLE_TOUCH, 30: HomeAction.QUICK_LOCK,
    31: HomeAction.PREVIOUS_PAGE, 33: HomeAction.LIBRARY, 34: HomeAction.SLEEP,
}
SIDE_FIELDS = {
    'sideButtonUpShort': 'readerUpTap', 'sideButtonUpLong': 'readerUpHold',
    'sideButtonDownShort': 'readerDownTap', 'sideButtonDownLong': 'readerDownHold',
}
HOME_FIELDS = ('homeButtonTapAction', 'homeButtonDoubleTapAction', 'homeButtonLongPressAction')


def reject_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON key')
        result[key] = value
    return result


def read_json(path: Path):
    with path.open('rb') as stream:
        data = stream.read(MAX_BYTES + 1)
    if len(data) > MAX_BYTES:
        raise ValueError('settings file exceeds 1 MiB')
    def reject_constant(_value):
        raise ValueError('invalid JSON constant')

    result = json.loads(data, object_pairs_hook=reject_duplicates, parse_constant=reject_constant)
    if not isinstance(result, dict):
        raise ValueError('settings file must be a JSON object')
    return result


def preview(legacy: dict, current: dict | None):
    candidates = []
    patch = {}

    def add(source, target, actions, allowed, reason):
        if source not in legacy:
            return
        raw = legacy[source]
        row = {'source': source, 'target': target}
        if type(raw) is not int or raw not in actions:
            row['status'] = 'unsupported_or_invalid'
            if type(raw) is int:
                row['legacy_id'] = raw
        else:
            action = actions[raw]
            row.update({'legacy_id': raw, 'action': action.name.lower(), 'target_id': int(action)})
            if not allowed:
                row['status'] = 'manual_review'
                row['reason'] = reason
            elif action.name == 'QUICK_ACTIONS':
                # The fork allows only one Quick Actions trigger and can reset
                # other bindings during normalization. Do not propose it alone.
                row['status'] = 'manual_review'
                row['reason'] = 'Quick Actions trigger ownership requires review'
            elif current is None:
                row['status'] = 'needs_current_settings'
            elif target not in current:
                row['status'] = 'proposed'
                patch[target] = int(action)
            elif type(current[target]) is int and current[target] == int(action):
                row['status'] = 'already_matching'
            else:
                row['status'] = 'existing_value_preserved'
        candidates.append(row)

    # CrossInk can rotate its physical side-button mappings. This fork does not
    # implement the same behavior, so do not suggest those conversions.
    side_ok = type(legacy.get('sideButtonOrientationAware')) is int and legacy['sideButtonOrientationAware'] == 0
    for source, target in SIDE_FIELDS.items():
        add(source, target, SIDE_ACTIONS, side_ok, 'side-button orientation behavior requires review')
    add('sideButtonChordAction', 'readerSideChord', CHORD_ACTIONS, side_ok,
        'side-button orientation behavior requires review')
    home_ok = type(legacy.get('homeButtonInReaderEnabled')) is int and legacy['homeButtonInReaderEnabled'] == 1
    for field in HOME_FIELDS:
        add(field, field, HOME_ACTIONS, home_ok, 'Home-key reader enablement requires review')
    return {
        'format': 1,
        'assumed_source_format': 'CrossInk 1.6.1',
        'purpose': 'preview only; no settings or device files were changed',
        'notice': 'Only listed shortcut fields are considered. Verify proposed behavior on the X4 Pro.',
        'candidates': candidates,
        'patch_for_missing_fields_only': patch,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--legacy', type=Path, required=True, help='CrossInk 1.6.1 JSON settings backup')
    parser.add_argument('--current', type=Path, help='fresh current fork JSON settings backup for comparison')
    args = parser.parse_args()
    try:
        legacy = read_json(args.legacy)
        current = read_json(args.current) if args.current else None
        print(json.dumps(preview(legacy, current), indent=2))
    except (OSError, ValueError, UnicodeError, RecursionError) as exc:
        print(f'Cannot read settings: {type(exc).__name__}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
