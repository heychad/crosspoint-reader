"""Check TLS patch reproducibility and refuse SDK/patch drift."""
import importlib.util
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('patch_tls',root/'scripts/patch_freeink_tls.py')
patch=importlib.util.module_from_spec(spec);spec.loader.exec_module(patch)


class TlsPatchTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.project=Path(self.temporary.name);self.sdk=self.project/'freeink-sdk'
        self.target=self.sdk/patch.TARGET;self.target.parent.mkdir(parents=True)
        source=subprocess.check_output(['git','show','HEAD:'+str(patch.TARGET)],cwd=root/'freeink-sdk')
        self.assertEqual(hashlib.sha256(source).hexdigest(),patch.ORIGINAL_SHA256)
        self.target.write_bytes(source)
        copied=self.project/patch.PATCH;copied.parent.mkdir(parents=True);shutil.copyfile(root/patch.PATCH,copied)
        self.git('init','-q');self.git('add','--',str(patch.TARGET))
        tree=self.git('write-tree').strip()
        commit=self.git('-c','user.name=Fixture','-c','user.email=fixture@example.invalid','commit-tree',tree,input='fixture\n').strip()
        self.git('update-ref','HEAD',commit);self.revision=commit

    def git(self,*args,input=None):
        return subprocess.run(['git',*args],cwd=self.sdk,input=input,text=True,capture_output=True,check=True).stdout

    def apply(self,revision=None):patch.apply_patch(self.project,expected_revision=revision or self.revision)

    def test_clean_application_and_repeated_application_match_pinned_hash(self):
        self.apply();first=self.target.read_bytes()
        self.assertEqual(hashlib.sha256(first).hexdigest(),patch.PATCHED_SHA256)
        self.apply();self.assertEqual(self.target.read_bytes(),first)

    def test_unknown_sdk_source_stays_untouched(self):
        self.target.write_bytes(self.target.read_bytes()+b'\n// unrelated edit\n');before=self.target.read_bytes()
        with self.assertRaisesRegex(RuntimeError,'unrecognized'):self.apply()
        self.assertEqual(self.target.read_bytes(),before)

    def test_modified_patch_is_rejected_before_source_changes(self):
        path=self.project/patch.PATCH;path.write_bytes(path.read_bytes()+b'\n')
        before=self.target.read_bytes()
        with self.assertRaisesRegex(RuntimeError,'altered'):self.apply()
        self.assertEqual(self.target.read_bytes(),before)

    def test_wrong_sdk_pin_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError,'revision'):self.apply('0'*40)
        self.assertEqual(hashlib.sha256(self.target.read_bytes()).hexdigest(),patch.ORIGINAL_SHA256)

    def test_escaping_target_symlink_is_rejected(self):
        external=self.project/'outside.cpp';external.write_bytes(self.target.read_bytes())
        self.target.unlink();self.target.symlink_to(external)
        with self.assertRaisesRegex(RuntimeError,'escaping'):self.apply()
        self.assertEqual(hashlib.sha256(external.read_bytes()).hexdigest(),patch.ORIGINAL_SHA256)

    def test_missing_patch_is_rejected(self):
        (self.project/patch.PATCH).unlink()
        with self.assertRaisesRegex(RuntimeError,'Missing'):self.apply()


if __name__=='__main__':unittest.main()
