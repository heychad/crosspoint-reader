"""Check native and RC3 positions against known records, including ambiguous pairs."""
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'scripts/preview_crossink_progress.py'
spec = importlib.util.spec_from_file_location('preview_crossink_progress', SCRIPT)
preview = importlib.util.module_from_spec(spec)
spec.loader.exec_module(preview)


def native(page, count=None, offset=None):
    raw = struct.pack('<HH', 3, page)
    if count is not None:
        raw += struct.pack('<H', count)
    if offset is not None:
        raw += struct.pack('<I', offset)
    return raw


def rc3(page, seq, offset=None):
    raw = native(page, 90, offset or 0) + struct.pack('<BBI', 0xc5, int(offset is not None), seq)
    return raw + struct.pack('<I', zlib.crc32(raw))


def record(data):
    return {'status': 'valid', **preview.decode(data)}


class ProgressTest(unittest.TestCase):
    def test_native_records_preserve_every_byte(self):
        for data in (native(65535), native(0, 0), native(1, 3, 0), native(27, 44, 0xffffffff)):
            self.assertEqual(bytes.fromhex(preview.decode(data)['native_hex']), data)

    def test_optional_offset_is_distinct_from_zero(self):
        for offset in (None, 0, 9383):
            result = preview.decode(rc3(5, 0, offset))
            self.assertEqual(result['text_offset'], offset)
            self.assertEqual(bytes.fromhex(result['native_hex']), native(5, 90, offset))

    def test_rejects_unknown_sizes_and_damaged_versioned_records(self):
        raw = rc3(5, 10)
        for bit in range(8 * len(raw)):
            edited = bytearray(raw)
            edited[bit // 8] ^= 1 << (bit % 8)
            with self.assertRaises(ValueError):
                preview.decode(edited)
        for length in set(range(22)) - {4, 6, 10, 20}:
            with self.assertRaises(ValueError):
                preview.decode(bytes(length))
        for index, value in [(10, 0), (11, 2)]:
            edited = bytearray(raw)
            edited[index] = value
            edited[16:] = struct.pack('<I', zlib.crc32(edited[:16]))
            with self.assertRaises(ValueError):
                preview.decode(edited)

    def test_sequence_order_and_conflicts(self):
        for a, b, expected in ((1, 2, 'backup'), (0, 0xffffffff, 'primary'),
                               (0xffffffff, 0, 'backup'), (8, 8, None), (0, 0x80000000, None)):
            first, second = record(rc3(2, a)), record(rc3(3, b))
            self.assertEqual(preview.choose(first, second)[0], expected)

    def test_native_ownership_and_file_fallback(self):
        first, second = record(native(2, 90)), record(native(3, 90))
        versioned = record(rc3(3, 2))
        self.assertEqual(preview.choose(first, second)[0], 'primary')
        self.assertIsNone(preview.choose(first, versioned)[0])
        self.assertEqual(preview.choose(versioned, first)[0], 'primary')
        self.assertEqual(preview.choose(second, versioned)[0], 'primary')
        for status in ('missing', 'invalid'):
            self.assertEqual(preview.choose({'status': status}, second)[0], 'backup')

    def test_optional_staged_record_never_decides_the_resume_position(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp) / 'epub_7'
            cache.mkdir()
            staged = cache / 'progress.bin.tmp'
            self.assertEqual(preview.preview(cache, inspect_staged=True)['staged']['status'], 'missing')
            for raw, status in [(native(37, 90, 1120), 'uncommitted'), (rc3(37, 4), 'invalid'),
                                (b'abc', 'invalid')]:
                staged.write_bytes(raw)
                self.assertNotIn('staged', preview.preview(cache))
                run = subprocess.run([sys.executable, SCRIPT, '--inspect-staged', cache], capture_output=True)
                self.assertEqual(run.returncode, 2)
                report = json.loads(run.stdout)
                self.assertIsNone(report['selected'])
                self.assertEqual(report['staged']['status'], status)
                if status == 'uncommitted':
                    self.assertEqual(report['staged']['native_hex'], raw.hex())
                self.assertEqual(staged.read_bytes(), raw)
            (cache / 'progress.bin').write_bytes(native(1, 90))
            staged.write_bytes(native(40, 90))
            report = preview.preview(cache, inspect_staged=True)
            self.assertEqual((report['selected'], report['primary']['page'], report['staged']['page']),
                             ('primary', 1, 40))
            staged.unlink()
            staged.mkdir()
            run = subprocess.run([sys.executable, SCRIPT, '--inspect-staged', cache], capture_output=True)
            self.assertEqual(run.returncode, 0)
            self.assertEqual(json.loads(run.stdout)['staged']['status'], 'unreadable')

    def test_command_only_reads_the_cache(self):
        with tempfile.TemporaryDirectory() as tmp:
            cache = Path(tmp) / 'epub_3'
            cache.mkdir()
            self.assertEqual(subprocess.run([sys.executable, SCRIPT, cache], capture_output=True).returncode, 2)
            primary = cache / 'progress.bin'
            backup = cache / 'progress.bin.bak'
            primary.write_bytes(bytes(8192))
            backup.write_bytes(rc3(0, 3, 0))
            before = {f.name: f.read_bytes() for f in cache.iterdir()}
            self.assertEqual(preview.preview(cache)['selected'], 'backup')
            self.assertEqual(subprocess.run([sys.executable, SCRIPT, cache], capture_output=True).returncode, 0)
            self.assertEqual(before, {f.name: f.read_bytes() for f in cache.iterdir()})
            backup.unlink()
            backup.mkdir()
            self.assertEqual(subprocess.run([sys.executable, SCRIPT, cache], capture_output=True).returncode, 1)


if __name__ == '__main__':
    unittest.main()
