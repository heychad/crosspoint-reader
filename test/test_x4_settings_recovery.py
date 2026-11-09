import importlib.util
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('recovery', ROOT / 'scripts/x4_settings_recovery.py')
recovery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recovery)


class SettingsRecoveryToolTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.card = self.root / 'card'
        self.folder = self.card / '.crosspoint'
        self.folder.mkdir(parents=True)
        (self.folder / 'settings.json').write_text('{"password":"secret"}')
        (self.folder / 'settings.json.tmp').write_bytes(b'{broken')
        (self.folder / 'settings.recovered-01.bak').write_text('{"font":14}')

    def test_export_is_verified_private_and_does_not_touch_card(self):
        before = recovery.read_candidates(self.card)
        destination = recovery.export(self.card, self.root / 'backup', before)
        self.assertEqual(recovery.read_candidates(self.card), before)
        self.assertEqual(set(os.listdir(destination)), set(before))
        for name, item in before.items():
            self.assertEqual((destination / name).read_bytes(), item['bytes'])
            self.assertEqual(stat.S_IMODE((destination / name).stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(destination.stat().st_mode), 0o700)
        self.assertFalse(before['settings.json.tmp']['json_object'])
        self.assertTrue(before['settings.json']['json_object'])

    def test_refuses_card_destination_and_existing_backup(self):
        data = recovery.read_candidates(self.card)
        with self.assertRaises(ValueError):
            recovery.export(self.card, self.card / 'backup', data)
        (self.root / 'existing').mkdir()
        with self.assertRaises(FileExistsError):
            recovery.export(self.card, self.root / 'existing', data)

    def test_refuses_symlinks_and_reports_changed_source(self):
        (self.folder / 'settings.json').unlink()
        (self.folder / 'settings.json').symlink_to(self.root / 'other')
        with self.assertRaises(OSError):
            recovery.read_candidates(self.card)
        (self.folder / 'settings.json').unlink()
        (self.folder / 'settings.json').write_text('{}')
        candidates = recovery.read_candidates(self.card)
        with mock.patch.object(recovery, 'read_candidates', return_value={}):
            with self.assertRaisesRegex(OSError, 'changed during export'):
                recovery.export(self.card, self.root / 'backup', candidates)
        self.assertEqual((self.root / 'backup/settings.json').read_text(), '{}')

    def test_cli_never_prints_settings_contents(self):
        result = subprocess.run(['python3', str(ROOT / 'scripts/x4_settings_recovery.py'), str(self.card)],
                                capture_output=True, text=True, check=True)
        self.assertIn('settings.json:', result.stdout)
        self.assertNotIn('secret', result.stdout)
        self.assertNotIn('password', result.stdout)

    def test_deep_and_nonstandard_json_are_preserved(self):
        (self.folder / 'settings.json.tmp').write_text('{"value":' + '[' * 2000 + '0' + ']' * 2000 + '}')
        (self.folder / 'settings.json.bak').write_text('{"value":NaN}')
        candidates = recovery.read_candidates(self.card)
        self.assertFalse(candidates['settings.json.bak']['json_object'])
        destination = recovery.export(self.card, self.root / 'backup', candidates)
        self.assertEqual((destination / 'settings.json.tmp').read_bytes(), candidates['settings.json.tmp']['bytes'])

    def test_restore_requires_matching_backup_and_preserves_all_originals(self):
        (self.folder / 'settings.json.bak').write_text('{"font":13}')
        candidates = recovery.read_candidates(self.card)
        backup = recovery.export(self.card, self.root / 'backup', candidates)
        self.assertIsNone(recovery.restore(self.card, backup, 'settings.json.bak'))
        self.assertEqual(recovery.read_candidates(self.card), candidates)
        archive = recovery.restore(self.card, backup, 'settings.json.bak', apply=True)
        self.assertEqual((self.folder / 'settings.json').read_text(), '{"font":13}')
        self.assertFalse((self.folder / 'settings.json.tmp').exists())
        self.assertFalse((self.folder / 'settings.json.bak').exists())
        for name in ('settings.json', 'settings.json.tmp', 'settings.json.bak'):
            self.assertEqual((archive / name).read_bytes(), candidates[name]['bytes'])
        self.assertEqual((archive / 'selected.copy').read_text(), '{"font":13}')
        self.assertEqual((self.folder / 'settings.recovered-01.bak').read_bytes(),
                         candidates['settings.recovered-01.bak']['bytes'])

    def test_restore_rejects_changed_backup_or_invalid_choice_before_writing(self):
        candidates = recovery.read_candidates(self.card)
        backup = recovery.export(self.card, self.root / 'backup', candidates)
        with self.assertRaisesRegex(ValueError, 'Choose'):
            recovery.restore(self.card, backup, 'settings.json.tmp', apply=True)
        (backup / 'settings.json').write_text('{"changed":true}')
        with self.assertRaisesRegex(ValueError, 'do not match'):
            recovery.restore(self.card, backup, 'settings.json', apply=True)
        self.assertEqual(recovery.read_candidates(self.card), candidates)
        self.assertFalse(any(self.folder.glob('settings-recovery-*')))

    def test_interrupted_moves_keep_export_and_original_bytes(self):
        for failure_at in (0, 1, 2):
            with self.subTest(failure_at=failure_at), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                card = root / 'card'
                folder = card / '.crosspoint'
                folder.mkdir(parents=True)
                for name, content in (('settings.json', '{"n":1}'), ('settings.json.bak', '{"n":2}'),
                                      ('settings.json.tmp', '{bad')):
                    (folder / name).write_text(content)
                candidates = recovery.read_candidates(card)
                backup = recovery.export(card, root / 'backup', candidates)
                real_rename = os.rename
                count = 0
                def interrupt(src, dst):
                    nonlocal count
                    if count == failure_at:
                        raise OSError('injected rename failure')
                    count += 1
                    return real_rename(src, dst)
                with mock.patch.object(recovery.os, 'rename', side_effect=interrupt):
                    with self.assertRaisesRegex(OSError, 'keep the card mounted'):
                        recovery.restore(card, backup, 'settings.json.bak', apply=True)
                archives = list(folder.glob('settings-recovery-*'))
                self.assertEqual(len(archives), 1)
                for name, item in candidates.items():
                    self.assertEqual((backup / name).read_bytes(), item['bytes'])
                    original = (folder / name) if (folder / name).exists() else (archives[0] / name)
                    self.assertEqual(original.read_bytes(), item['bytes'])

    def test_interrupted_writes_keep_export_and_original_bytes(self):
        for failure_at in (0, 1):
            with self.subTest(failure_at=failure_at), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                card = root / 'card'
                folder = card / '.crosspoint'
                folder.mkdir(parents=True)
                (folder / 'settings.json').write_text('{"n":1}')
                (folder / 'settings.json.bak').write_text('{"n":2}')
                candidates = recovery.read_candidates(card)
                backup = recovery.export(card, root / 'backup', candidates)
                real_fsync = os.fsync
                count = 0
                def interrupt(fd):
                    nonlocal count
                    if count == failure_at:
                        raise OSError('injected sync failure')
                    count += 1
                    return real_fsync(fd)
                with mock.patch.object(recovery.os, 'fsync', side_effect=interrupt):
                    with self.assertRaisesRegex(OSError, 'keep the card mounted'):
                        recovery.restore(card, backup, 'settings.json.bak', apply=True)
                archives = list(folder.glob('settings-recovery-*'))
                self.assertEqual(len(archives), 1)
                for name, item in candidates.items():
                    self.assertEqual((backup / name).read_bytes(), item['bytes'])
                    archived = archives[0] / name
                    if archived.exists():
                        self.assertEqual(archived.read_bytes(), item['bytes'])
                    else:
                        self.assertEqual((folder / name).read_bytes(), item['bytes'])

    def test_user_interrupt_reports_recovery_locations(self):
        candidates = recovery.read_candidates(self.card)
        backup = recovery.export(self.card, self.root / 'backup', candidates)
        with mock.patch.object(recovery.os, 'rename', side_effect=KeyboardInterrupt):
            with self.assertRaisesRegex(OSError, 'keep the card mounted'):
                recovery.restore(self.card, backup, 'settings.json', apply=True)
        self.assertEqual(recovery.read_candidates(self.card), candidates)
        for name, item in candidates.items():
            self.assertEqual((backup / name).read_bytes(), item['bytes'])


if __name__ == '__main__':
    unittest.main()
