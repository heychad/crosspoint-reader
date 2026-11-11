"""Verify SDK patch safety, pinning, and repeatable application."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('patch_freeink', ROOT / 'scripts/patch_freeink_sdmmc.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
fixture_spec = importlib.util.spec_from_file_location('sdmmc_fixture', ROOT / 'test/sdmmc_monitor/fixture.py')
fixture = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(fixture)
original = subprocess.run(['git', '-C', str(ROOT / 'freeink-sdk'), 'show',
                           'HEAD:' + str(module.TARGET)], capture_output=True, check=True).stdout

class PatchTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.project = Path(self.tmp.name)
        self.sdk = self.project / 'freeink-sdk'
        self.sdk.mkdir()
        self.target = self.sdk / module.TARGET
        self.target.parent.mkdir(parents=True)
        self.target.write_bytes(original)
        patch = self.project / module.PATCH
        patch.parent.mkdir(parents=True)
        shutil.copyfile(ROOT / module.PATCH, patch)
        subprocess.run(['git', 'init', '-q', str(self.sdk)], check=True)
        subprocess.run(['git', '-C', str(self.sdk), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.sdk), '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
                        'commit', '-qm', 'fixture'], check=True)
        self.revision = subprocess.run(['git', '-C', str(self.sdk), 'rev-parse', 'HEAD'],
                                       capture_output=True, text=True, check=True).stdout.strip()

    def test_apply_and_repeat(self):
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.digest(self.target), module.PATCHED_SHA256)
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.digest(self.target), module.PATCHED_SHA256)

    def test_host_fixture_from_unpatched_source(self):
        prior_root = fixture.ROOT
        self.addCleanup(setattr, fixture, 'ROOT', prior_root)
        fixture.ROOT = self.project
        self.assertEqual(self.target.read_bytes(), original)
        self.assertEqual(module.digest(self.target), module.ORIGINAL_SHA256)
        self.assertEqual(module.hashlib.sha256(fixture.patched_source().encode()).hexdigest(), module.PATCHED_SHA256)
        self.assertEqual(self.target.read_bytes(), original)

    def test_unknown_changes_are_preserved(self):
        self.target.write_bytes(original + b'\n// user change\n')
        before = self.target.read_bytes()
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(self.target.read_bytes(), before)

    def test_wrong_revision_is_rejected(self):
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision='0' * 40)
        self.assertEqual(self.target.read_bytes(), original)

    def test_altered_patch_is_rejected(self):
        (self.project / module.PATCH).write_bytes(b'not the approved patch')
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(self.target.read_bytes(), original)

    def test_escaping_source_is_rejected(self):
        self.target.unlink()
        outside = self.project / 'outside'
        outside.write_bytes(original)
        self.target.symlink_to(outside)
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(outside.read_bytes(), original)

if __name__ == '__main__':
    unittest.main()
