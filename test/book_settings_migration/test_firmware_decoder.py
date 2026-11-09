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
for version in range(1, 11):
    data = test_preview.record(version)
    records.extend(data[:length] for length in range(len(data)))
    records.extend((data, data + b'\0'))
for bit in range(19):
    records.append(test_preview.record(10, mask=1 << bit))
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
            expected = module.decode(data)['preview']
        except ValueError:
            assert line == 'invalid', index
            continue
        values = [int(value) for value in line.split()]
        actual = {name: values[position + 1] for position, name in enumerate(FIELDS)
                  if values[0] & (1 << position)}
        assert actual == expected, (index, actual, expected)
print(f'Firmware decoder agrees on {len(records)} reference, truncation, flag, and mask cases')
