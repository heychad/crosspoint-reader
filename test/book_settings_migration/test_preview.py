"""Exercise the read-only CrossInk binary settings decoder."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('preview', ROOT / 'scripts/preview_crossink_book_settings.py')
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


def record(version, flags=1, mask=None, image_grayscale=1, character_spacing=5, publisher_fonts=0, indexing_method=2):
    if mask is None:
        mask = (1 << (19 if version < 11 else version + 9)) - 1
    if version == 1:
        return bytes([1, 45, 0])
    data = bytearray([version, flags, 45, 0, 2, 3, 18, 110])
    if version >= 3:
        data.append(4)  # word spacing
    data.append(1)  # orientation
    data.extend([7, 8] if version >= 8 else [7])
    data.extend([1, 2, 1, 0, 1])  # page number, alignment, style, hyphenation, AA
    if version <= 8:
        data.append(1)  # obsolete dark mode
    data.extend([2, 1, 0, 1, 0, 1])  # image, spacing, indent, focus, guide, render
    if version >= 4:
        data.append(indexing_method)
    data.extend(b'Font\0' + bytes(59))
    if version >= 5:
        data.extend(b'Dict\0' + bytes(59))
    if version >= 7:
        data.append(16)
    if version >= 10:
        data.extend(mask.to_bytes(4, 'little'))
    if version >= 11:
        data.append(image_grayscale)
    if version >= 12:
        data.append(character_spacing)
    if version >= 13:
        data.append(publisher_fonts)
    return bytes(data)


class DecodeTest(unittest.TestCase):
    def test_all_versions_and_truncation(self):
        for version in range(1, 14):
            data = record(version)
            self.assertEqual(preview.decode(data)['version'], version)
            for length in range(len(data)):
                with self.subTest(version=version, length=length):
                    with self.assertRaises(ValueError):
                        preview.decode(data[:length])
            with self.assertRaises(ValueError):
                preview.decode(data + b'\0')

    def test_v10_only_owned_compatible_values_are_proposed(self):
        result = preview.decode(record(10, mask=(1 << 4) | (1 << 6) | (1 << 8)))
        self.assertEqual(result['preview'], {'orientation': 1, 'screenMarginHorizontal': 8, 'paragraphAlignment': 2})
        self.assertEqual(result['unmapped'], [])
        self.assertNotIn('embeddedStyle', result['overrides'])

    def test_flag_off_does_not_invent_overrides(self):
        result = preview.decode(record(10, flags=0))
        self.assertEqual(result['preview'], {})
        self.assertEqual(result['overrides'], {})

    def test_point_size_and_exact_line_height(self):
        result = preview.decode(record(10, mask=(1 << 1) | (1 << 2)))
        self.assertEqual(result['preview'], {'fontSize': 18, 'lineHeight': 1})
        self.assertEqual(result['firmware_import_excluded'], ['fontSize'])
        data = bytearray(record(10, mask=(1 << 1) | (1 << 2)))
        data[6], data[7] = 3, 111  # Old font slot and non-native line height.
        result = preview.decode(bytes(data))
        self.assertEqual(result['preview'], {})
        self.assertEqual(result['unmapped'], ['readerFontPointSize', 'lineHeightPercent'])

    def test_margin_axes_map_independently(self):
        result = preview.decode(record(10, mask=3 << 5))
        self.assertEqual(result['preview'], {'screenMarginVertical': 7, 'screenMarginHorizontal': 8})
        single = preview.decode(record(10, mask=1 << 5))
        self.assertEqual(single['preview'], {'screenMarginVertical': 7})

    def test_only_natural_word_spacing_maps(self):
        data = bytearray(record(10, mask=1 << 3))
        data[8] = 0
        self.assertEqual(preview.decode(bytes(data))['preview'], {'wordSpacing': 100})
        data[8] = 1
        self.assertEqual(preview.decode(bytes(data))['unmapped'], ['wordSpacing'])

    def test_rejects_unknown_version_flags_and_masks(self):
        for data in (b'\x0e', record(10, flags=0x80), record(10, mask=1 << 20)):
            with self.assertRaises(ValueError):
                preview.decode(data)

    def test_rc_masks_and_image_ownership(self):
        both = (1 << 12) | (1 << 18)
        for version in (11, 12, 13):
            data = bytearray(record(version, mask=both, image_grayscale=0))
            data[17] = 0  # image rendering field in versions with split margins and no dark-mode byte
            self.assertEqual(preview.decode(bytes(data))['preview'], {'imageRendering': 3})
            data = bytearray(record(version, mask=1 << 12))
            data[17] = 0
            self.assertEqual(preview.decode(bytes(data))['preview'], {})
            self.assertEqual(preview.decode(record(version, mask=1 << 19))['preview'], {'sdFontFamilyName': 'Font'})

    def test_named_font_and_size_require_font_ownership(self):
        for version in range(2, 14):
            bit = 18 if version < 11 else 19
            result = preview.decode(record(version, mask=(1 << bit) | (1 << 1)))
            self.assertEqual(result['preview']['sdFontFamilyName'], 'Font')
            self.assertEqual(result['firmware_import_excluded'], [])
        for name in (b'', b'a' * 32, b'.hidden', b'bad/name', b'bad\\name', b'bad\x1fname'):
            data = bytearray(record(10, mask=(1 << 18) | (1 << 1)))
            offset = data.index(b'Font\0')
            data[offset:offset + 64] = (name + b'\0').ljust(64, b'\0')
            result = preview.decode(bytes(data))
            self.assertNotIn('sdFontFamilyName', result['preview'])
            self.assertEqual(result['firmware_import_excluded'], ['fontSize'])

    def test_character_spacing_requires_exact_whole_pixels(self):
        for value in range(11):
            result = preview.decode(record(12, mask=1 << 20, character_spacing=value))
            expected = {'characterSpacing': (value - 1) // 2} if value in (1, 3, 5, 7, 9) else {}
            self.assertEqual(result['preview'], expected)
        for version, kwargs in ((11, {'image_grayscale': 2}), (12, {'character_spacing': 11}),
                                (13, {'publisher_fonts': 3})):
            with self.assertRaises(ValueError):
                preview.decode(record(version, **kwargs))

    def test_explicit_render_mode_is_independent_of_snapshot(self):
        for version in range(2, 14):
            for flags in (0, 1, 4, 5):
                for mode in (0, 1, 2, 3, 255):
                    data = bytearray(record(version, flags=flags, mask=0))
                    data[4] = mode
                    result = preview.decode(bytes(data))
                    if flags & 4 and mode <= 2:
                        self.assertEqual(result['preview']['epubRenderMode'], mode)
                        self.assertNotIn('renderMode', result['unmapped'])
                    else:
                        self.assertNotIn('epubRenderMode', result['preview'])
                        self.assertEqual('renderMode' in result['unmapped'], bool(flags & 4))

    def test_current_preferences_take_precedence(self):
        result = preview.decode(record(10, mask=(1 << 1) | (1 << 2) | (1 << 4)))
        self.assertEqual(preview.compare_current(result, None)['patch_for_missing_fields_only'], {})
        comparison = preview.compare_current(result, {'version': 1, 'overrides': {'fontSize': 22, 'orientation': 1}})
        self.assertEqual(comparison['fields'], {'fontSize': 'existing_value_preserved',
                                               'lineHeight': 'missing_requires_manual_review', 'orientation': 'already_matching'})
        self.assertEqual(comparison['patch_for_missing_fields_only'], {})
        for invalid in ({'version': True, 'overrides': {}}, {'version': 1, 'overrides': []}):
            with self.assertRaises(ValueError):
                preview.compare_current(result, invalid)

    def test_duplicate_current_keys_are_rejected(self):
        with self.assertRaises(ValueError):
            preview.unique_object([('fontSize', 14), ('fontSize', 16)])

    def test_safe_mode_never_proposes_a_patch(self):
        result = preview.decode(record(10, flags=0x11))
        comparison = preview.compare_current(result, {'version': 1, 'overrides': {}})
        self.assertEqual(comparison['status'], 'safe_mode_requires_review')
        self.assertEqual(comparison['patch_for_missing_fields_only'], {})

    def test_unterminated_name_is_rejected(self):
        data = bytearray(record(10))
        name = data.index(b'Font\0')
        data[name:name + 64] = b'A' * 64
        with self.assertRaises(ValueError):
            preview.decode(data)

if __name__ == '__main__':
    unittest.main()
