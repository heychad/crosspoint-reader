"""Verify the mounted-volume patch and clean-checkout test fixture."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('volume_patch', ROOT / 'scripts/patch_freeink_volume.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
fixture_spec = importlib.util.spec_from_file_location('sdmmc_fixture', ROOT / 'test/sdmmc_monitor/fixture.py')
fixture = importlib.util.module_from_spec(fixture_spec)
fixture_spec.loader.exec_module(fixture)
original = subprocess.run(['git', '-C', str(ROOT / 'freeink-sdk'), 'show', 'HEAD:' + str(module.TARGET)],
                          capture_output=True, check=True).stdout


class VolumePatchTest(unittest.TestCase):
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
        shutil.copyfile(ROOT / module.MIGRATION_PATCH, self.project / module.MIGRATION_PATCH)
        shutil.copyfile(ROOT / module.GEOMETRY_MIGRATION_PATCH, self.project / module.GEOMETRY_MIGRATION_PATCH)
        subprocess.run(['git', 'init', '-q', str(self.sdk)], check=True)
        subprocess.run(['git', '-C', str(self.sdk), 'add', '.'], check=True)
        subprocess.run(['git', '-C', str(self.sdk), '-c', 'user.name=test', '-c', 'user.email=test@example.invalid',
                        'commit', '-qm', 'fixture'], check=True)
        self.revision = subprocess.run(['git', '-C', str(self.sdk), 'rev-parse', 'HEAD'],
                                       capture_output=True, text=True, check=True).stdout.strip()

    def test_apply_and_repeat(self):
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.patcher.digest(self.target), module.PATCHED_SHA256)
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.patcher.digest(self.target), module.PATCHED_SHA256)

    def test_upgrade_previous_type_only_patch(self):
        old = original.replace(b'  bool ready() const;\n',
                               b'  bool ready() const;\n  // SdFat volume type, or zero when not mounted.\n'
                               b'  uint8_t filesystemType() { return initialized ? vol().fatType() : 0; }\n')
        self.target.write_bytes(old)
        self.assertEqual(module.patcher.digest(self.target), module.PREVIOUS_SHA256)
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.patcher.digest(self.target), module.PATCHED_SHA256)
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.patcher.digest(self.target), module.PATCHED_SHA256)

    def test_unrecognized_source_is_preserved(self):
        changed = original + b'\n// local change\n'
        self.target.write_bytes(changed)
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(self.target.read_bytes(), changed)

    def test_upgrade_geometry_patch(self):
        module.apply_patch(self.project, expected_revision=self.revision)
        subprocess.run(['git', 'apply', '--reverse', str(self.project / module.GEOMETRY_MIGRATION_PATCH)],
                       cwd=self.sdk, check=True)
        self.assertEqual(module.patcher.digest(self.target), module.GEOMETRY_SHA256)
        module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(module.patcher.digest(self.target), module.PATCHED_SHA256)

    def test_upgrade_rejects_damaged_patch_without_changes(self):
        old = original.replace(b'  bool ready() const;\n',
                               b'  bool ready() const;\n  // SdFat volume type, or zero when not mounted.\n'
                               b'  uint8_t filesystemType() { return initialized ? vol().fatType() : 0; }\n')
        self.target.write_bytes(old)
        (self.project / module.MIGRATION_PATCH).write_bytes(b'bad patch')
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(self.target.read_bytes(), old)

    def test_bad_patch_and_revision_fail(self):
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision='0' * 40)
        (self.project / module.PATCH).write_bytes(b'bad patch')
        with self.assertRaises(RuntimeError):
            module.apply_patch(self.project, expected_revision=self.revision)
        self.assertEqual(self.target.read_bytes(), original)

    def test_fixture_does_not_modify_unpatched_sdk(self):
        prior = fixture.ROOT
        self.addCleanup(setattr, fixture, 'ROOT', prior)
        fixture.ROOT = self.project
        patched = fixture.patched_source(module).encode()
        self.assertEqual(module.patcher.hashlib.sha256(patched).hexdigest(), module.PATCHED_SHA256)
        self.assertEqual(self.target.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
