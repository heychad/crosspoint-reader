"""Compare the firmware decoder with the independently formatted reference records."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import test_preview

ROOT = Path(__file__).resolve().parents[2]
module = test_preview.preview
FIELDS = ('fontSize', 'lineHeight', 'wordSpacing', 'characterSpacing', 'orientation', 'screenMargin',
          'paragraphAlignment', 'extraParagraphSpacing', 'paragraphIndentSpaces', 'hyphenationEnabled',
          'embeddedStyle', 'textAntiAliasing', 'imageRendering', 'focusReadingEnabled', 'epubRenderMode')
records = []
for version in range(1, 14):
    data = test_preview.record(version)
    records.extend(data[:length] for length in range(len(data)))
    records.extend((data, data + b'\0'))
for version in range(10, 14):
    for bit in range(19 if version == 10 else version + 9):
        records.append(test_preview.record(version, mask=1 << bit))
for version in (11, 12, 13):
    records.append(test_preview.record(version, mask=1 << (version + 9)))
    records.append(test_preview.record(version, flags=0))
records.append(test_preview.record(13, publisher_fonts=3))
for method in (0, 1, 2, 255):
    records.append(test_preview.record(10, mask=1 << 17, indexing_method=method))
for version in range(2, 14):
    for flags in (0, 1, 4, 5, 0x14):
        for mode in (0, 1, 2, 3, 255):
            data = bytearray(test_preview.record(version, flags=flags, mask=0))
            data[4] = mode
            records.append(bytes(data))
for value in range(12):
    records.append(test_preview.record(12, mask=1 << 20, character_spacing=value))
for gray in range(3):
    data = bytearray(test_preview.record(11, mask=(1 << 12) | (1 << 18), image_grayscale=gray))
    data[17] = 0
    records.append(bytes(data))
for version in (3, 10):
    for spacing in (0, 1, 4, 255):
        data = bytearray(test_preview.record(version, mask=1 << 3))
        data[8] = spacing
        records.append(bytes(data))
records.extend((test_preview.record(10, flags=0), test_preview.record(10, flags=0x80),
                test_preview.record(10, mask=1 << 20)))
for name in (b'caf\xc3\xa9', b'\xc0\xaf', b'\xed\xa0\x80', b'\xf4\x90\x80\x80', b'\xe2\x82'):
    data = bytearray(test_preview.record(10))
    index = data.index(b'Font\0')
    data[index:index + 64] = (name + b'\0').ljust(64, b'\0')
    records.append(bytes(data))
with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / 'probe'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', str(ROOT / 'src/personalization'),
                    str(ROOT / 'test/book_settings_migration/decoder_probe.cpp'),
                    str(ROOT / 'src/personalization/CrossInkBookSettings.cpp'), '-o', str(executable)], check=True)
    proc = subprocess.run([str(executable)], input=''.join(data.hex() + '\n' for data in records),
                          text=True, capture_output=True, check=True)
    lines = proc.stdout.splitlines()
    assert len(lines) == len(records)
    for index, (data, line) in enumerate(zip(records, lines)):
        try:
            report = module.decode(data)
            expected = report['preview']
        except ValueError:
            assert line == 'invalid', index
            continue
        values = [int(value) for value in line.split()]
        actual = {name: values[position + 1] for position, name in enumerate(FIELDS)
                  if values[0] & (1 << position)}
        if values[0] & (1 << 17):
            actual['indexingMethod'] = values[18]
        assert actual == expected, (index, actual, expected)
        version = data[0]
        fields = module.MASK_FIELDS
        if version >= 11:
            fields = fields[:18] + ('imageGrayscale', 'sdFontFamilyName')
        if version >= 12:
            fields += ('characterSpacing',)
        if version >= 13:
            fields += ('publisherFonts',)
        expected_unmapped = sum(1 << i for i, name in enumerate(fields) if name in report['unmapped'])
        assert values[16] == expected_unmapped, (index, values[16], expected_unmapped)
        expected_flags = (2 if report['autoPageTurnSeconds'] else 0) if version == 1 else data[1] & ~1
        if 'epubRenderMode' in expected:
            expected_flags &= ~4
        assert values[17] == expected_flags, (index, values[17], expected_flags)
print(f'Firmware decoder agrees on {len(records)} reference, truncation, flag, and mask cases')
