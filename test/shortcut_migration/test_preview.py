"""Verify the read-only CrossInk shortcut preview and its ID contracts."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/preview_crossink_shortcuts.py'
spec = importlib.util.spec_from_file_location('shortcut_preview', SCRIPT)
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


def cpp_enum(path, enum_name):
    source = path.read_text()
    body = source.split(f'enum class {enum_name} : uint8_t {{', 1)[1].split('};', 1)[0]
    result = {}
    for number, item in enumerate(body.split(',')):
        name = item.strip()
        if name:
            result[name] = number
    return result


class PreviewTests(unittest.TestCase):
    def test_target_ids_match_firmware_enums(self):
        reader = cpp_enum(ROOT / 'src/personalization/ReaderShortcuts.h', 'Action')
        home = cpp_enum(ROOT / 'src/util/HomeButtonInput.h', 'HomeButtonAction')
        names = {
            'DISABLED': 'Disabled', 'PREVIOUS_PAGE': 'PreviousPage', 'NEXT_PAGE': 'NextPage',
            'PREVIOUS_CHAPTER': 'PreviousChapter', 'NEXT_CHAPTER': 'NextChapter',
            'BOOKMARK': 'Bookmark', 'DICTIONARY': 'Dictionary', 'FOOTNOTES': 'Footnotes', 'REFRESH': 'Refresh', 'TOGGLE_FRONTLIGHT': 'ToggleFrontlight',
            'ROTATE': 'Rotate', 'ROTATE_BACKWARD': 'RotateBackward', 'LARGER_TEXT': 'LargerText',
            'SMALLER_TEXT': 'SmallerText', 'ROTATE_HALF_TURN': 'RotateHalfTurn', 'QUICK_ACTIONS': 'QuickActions',
            'FILE_BROWSER': 'FileBrowser', 'LIBRARY': 'Library', 'READING_STATISTICS': 'ReadingStatistics',
            'SCREENSHOT': 'Screenshot', 'SYNC_PROGRESS': 'SyncProgress', 'CREATE_CLIPPING': 'CreateClipping',
            'TOGGLE_NIGHT': 'ToggleNight', 'TOGGLE_FOCUS': 'ToggleFocus', 'TOGGLE_TOUCH': 'ToggleTouch', 'TOGGLE_TILT': 'ToggleTilt', 'QUICK_LOCK': 'QuickLock',
            'TOGGLE_HOME_BUTTON': 'ToggleHomeButton', 'TOGGLE_COMPLETION': 'ToggleCompletion',
            'SLEEP': 'Sleep', 'FILE_TRANSFER': 'FileTransfer', 'CYCLE_FONT_FAMILY': 'CycleFontFamily',
            'HOME_READER': 'HomeReader',
        }
        for action in preview.ReaderAction:
            self.assertEqual(action.value, reader[names[action.name]])
        home_names = dict(names, HOME='Home', IGNORE='Ignore', READER_MENU='ReaderMenu', SYNC_PROGRESS='Sync')
        for action in preview.HomeAction:
            self.assertEqual(action.value, home[home_names[action.name]])

    def test_home_frontlight_power_action(self):
        legacy = {'homeButtonInReaderEnabled': 1, 'homeButtonTapAction': 28}
        result = preview.preview(legacy, {})
        self.assertEqual(result['patch_for_missing_fields_only']['homeButtonTapAction'],
                         preview.HomeAction.TOGGLE_FRONTLIGHT)

    def test_distinct_legacy_enum_families_and_existing_values(self):
        legacy = {'sideButtonOrientationAware': 0, 'homeButtonInReaderEnabled': 1,
                  'sideButtonUpShort': 2, 'sideButtonUpLong': 8, 'sideButtonDownLong': 1,
                  'sideButtonChordAction': 20, 'homeButtonTapAction': 23,
                  'homeButtonDoubleTapAction': 27, 'homeButtonLongPressAction': 25}
        result = preview.preview(legacy, {'readerUpTap': 2, 'homeButtonTapAction': 0})
        by_source = {row['source']: row for row in result['candidates']}
        self.assertEqual(by_source['sideButtonUpShort']['status'], 'existing_value_preserved')
        self.assertEqual(by_source['homeButtonTapAction']['status'], 'already_matching')
        self.assertEqual(result['patch_for_missing_fields_only'], {
            'readerUpHold': 27, 'readerDownHold': 35, 'readerSideChord': 29,
            'homeButtonLongPressAction': 9})
        self.assertEqual(by_source['homeButtonDoubleTapAction']['status'], 'manual_review')
        self.assertEqual(by_source['homeButtonDoubleTapAction']['target_id'], 12)

    def test_added_home_actions_match_importer(self):
        for raw, expected in ((1, 24), (7, 7), (11, 20), (14, 23), (15, 21), (16, 4), (29, 22), (31, 19), (34, 24)):
            data = {'homeButtonInReaderEnabled': 1, 'homeButtonTapAction': raw}
            result = preview.preview(data, {})
            self.assertEqual(result['patch_for_missing_fields_only']['homeButtonTapAction'], expected)

    def test_no_current_file_produces_no_patch(self):
        data = {'sideButtonOrientationAware': 0, 'sideButtonUpShort': 2}
        result = preview.preview(data, None)
        self.assertEqual(result['patch_for_missing_fields_only'], {})
        self.assertEqual(result['candidates'][0]['status'], 'needs_current_settings')

    def test_font_switch_maps_side_and_chord_to_font_cycle(self):
        legacy = {'sideButtonOrientationAware': 0, 'sideButtonUpShort': 4, 'sideButtonChordAction': 11,
                  'homeButtonInReaderEnabled': 1, 'homeButtonTapAction': 4}
        result = preview.preview(legacy, {})
        self.assertEqual(result['patch_for_missing_fields_only'], {'readerUpTap': 39, 'readerSideChord': 39,
                                                                    'homeButtonTapAction': 26})

    def test_home_reader_mappings_use_distinct_legacy_ids(self):
        legacy = {'sideButtonOrientationAware': 0, 'sideButtonUpShort': 36, 'sideButtonChordAction': 32}
        result = preview.preview(legacy, {})
        self.assertEqual(result['patch_for_missing_fields_only'], {'readerUpTap': 42, 'readerSideChord': 42})

    def test_unknown_orientation_home_enablement_and_bad_actions_fail_closed(self):
        data = {'sideButtonOrientationAware': 1, 'sideButtonUpShort': 2,
                'homeButtonInReaderEnabled': 0, 'homeButtonTapAction': 23,
                'sideButtonDownShort': True, 'sideButtonUpLong': 255}
        result = preview.preview(data, {})
        self.assertEqual(result['patch_for_missing_fields_only'], {})
        by_source = {row['source']: row['status'] for row in result['candidates']}
        self.assertEqual(by_source['sideButtonUpShort'], 'manual_review')
        self.assertEqual(by_source['homeButtonTapAction'], 'manual_review')
        self.assertEqual(by_source['sideButtonDownShort'], 'unsupported_or_invalid')
        self.assertEqual(by_source['sideButtonUpLong'], 'unsupported_or_invalid')

    def test_json_rejects_duplicates_oversized_or_non_object(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'settings.json'
            for value in (b'{"a":1,"a":2}', b'[]', b'{"x":NaN}', b' ' * (preview.MAX_BYTES + 1)):
                path.write_bytes(value)
                with self.assertRaises(ValueError):
                    preview.read_json(path)

    def test_cli_does_not_print_credentials_or_write_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            legacy = Path(directory) / 'legacy.json'
            current = Path(directory) / 'current.json'
            legacy.write_text(json.dumps({'sideButtonOrientationAware': 0, 'sideButtonUpShort': 2,
                                          'password': 'test-private-value'}))
            current.write_text(json.dumps({'unrelated': 'another-private-value'}))
            before = (legacy.read_bytes(), current.read_bytes())
            result = subprocess.run([sys.executable, str(SCRIPT), '--legacy', str(legacy), '--current', str(current)],
                                    check=True, capture_output=True, text=True)
            self.assertNotIn('private-value', result.stdout + result.stderr)
            self.assertEqual(json.loads(result.stdout)['patch_for_missing_fields_only'], {'readerUpTap': 3})
            self.assertEqual(before, (legacy.read_bytes(), current.read_bytes()))


if __name__ == '__main__':
    unittest.main()
