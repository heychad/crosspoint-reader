import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('backup_reader_card', ROOT / 'scripts/backup_reader_card.py')
backup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backup)


class BackupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.reader = self.root / 'reader'
        (self.reader / '.crosspoint' / 'empty').mkdir(parents=True)
        (self.reader / '.crosspoint' / '.hidden').write_bytes(b'progress')
        (self.reader / '.crossink-stats-backup').mkdir()
        (self.reader / '.crossink-stats-backup' / 'stats').write_bytes(b'stats')
        self.destination = self.root / 'backup'

    def test_copies_hidden_and_empty_directories_with_matching_hashes(self):
        self.assertEqual(backup.backup(self.reader, self.destination), 2)
        self.assertEqual(backup.inventory(self.reader), backup.inventory(self.destination))
        self.assertTrue((self.destination / 'backup-receipt.json').is_file())
        self.assertEqual(self.destination.stat().st_mode & 0o077, 0)

    def test_rejects_existing_destination_and_nested_destination(self):
        self.destination.mkdir()
        with self.assertRaises(FileExistsError):
            backup.backup(self.reader, self.destination)
        with self.assertRaises(ValueError):
            backup.backup(self.reader, self.reader / 'backup')

    def test_refuses_symlink_before_creating_backup(self):
        (self.reader / '.crosspoint' / 'link').symlink_to(self.root)
        with self.assertRaises(ValueError):
            backup.backup(self.reader, self.destination)
        self.assertFalse(self.destination.exists())

    def test_reader_change_stops_verification_without_receipt(self):
        real_copy = backup.shutil.copytree
        def changed(*args, **kwargs):
            result = real_copy(*args, **kwargs)
            (self.reader / '.crosspoint' / '.hidden').write_bytes(b'changed')
            return result
        with patch.object(backup.shutil, 'copytree', side_effect=changed):
            with self.assertRaises(ValueError):
                backup.backup(self.reader, self.destination)
        self.assertFalse((self.destination / 'backup-receipt.json').exists())

    def test_copy_failure_keeps_original_state_and_no_receipt(self):
        before = backup.inventory(self.reader)
        with patch.object(backup.shutil, 'copytree', side_effect=OSError('injected copy failure')):
            with self.assertRaises(OSError):
                backup.backup(self.reader, self.destination)
        self.assertEqual(backup.inventory(self.reader), before)
        self.assertFalse((self.destination / 'backup-receipt.json').exists())

    def test_rejects_non_directory_and_missing_state(self):
        (self.reader / '.crosspoint' / '.hidden').unlink()
        (self.reader / '.crosspoint' / 'empty').rmdir()
        (self.reader / '.crosspoint').rmdir()
        with self.assertRaises(ValueError):
            backup.backup(self.reader, self.destination)
        (self.reader / '.crosspoint').write_text('not a directory')
        with self.assertRaises(ValueError):
            backup.backup(self.reader, self.destination)
        self.assertFalse(self.destination.exists())


if __name__ == '__main__':
    unittest.main()
