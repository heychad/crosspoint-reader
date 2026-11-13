"""Keep cached local dependencies bound to the current firmware checkout."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('local_links',root/'scripts/repair_local_sdk_links.py')
links=importlib.util.module_from_spec(spec);spec.loader.exec_module(links)


class LocalLinkTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name);self.deps=self.root/'.pio/libdeps'
        self.folder=self.deps/'x4pro';self.folder.mkdir(parents=True)
        (self.root/'freeink-sdk/libs/network/SecureNet').mkdir(parents=True)
        self.path=self.folder/'SecureNet.pio-link'
        self.value={'cwd':'/different/checkout','spec':{'uri':'symlink://freeink-sdk/libs/network/SecureNet','name':'SecureNet'},'other':'keep'}
        self.path.write_text(json.dumps(self.value))

    def repair(self,environment='x4pro'):return links.repair_links(self.root,self.deps,environment)

    def test_rebinds_only_sdk_paths_and_is_idempotent(self):
        other=self.folder/'Other.pio-link';other.write_text('{"cwd":"/other","spec":{"uri":"symlink://local-tool"}}')
        before=other.read_bytes();self.assertEqual(self.repair(),1)
        expected={**self.value,'cwd':str(self.root.resolve())}
        self.assertEqual(json.loads(self.path.read_text()),expected)
        self.assertEqual(other.read_bytes(),before);self.assertEqual(self.repair(),0)
        self.assertEqual(list(self.folder.glob('.sdk-link-*')),[])

    def test_rejects_escaping_or_unknown_sdk_target(self):
        for uri in ('symlink://freeink-sdk/../outside','symlink://freeink-sdk/absent'):
            self.value['spec']['uri']=uri;self.path.write_text(json.dumps(self.value));before=self.path.read_bytes()
            with self.assertRaises(RuntimeError):self.repair()
            self.assertEqual(self.path.read_bytes(),before)

    def test_rejects_symlink_metadata(self):
        target=self.root/'external-link';target.write_bytes(self.path.read_bytes())
        self.path.unlink();self.path.symlink_to(target)
        with self.assertRaises(RuntimeError):self.repair()
        self.assertEqual(json.loads(target.read_text())['cwd'],'/different/checkout')

    def test_rejects_external_sdk(self):
        import shutil
        shutil.rmtree(self.root/'freeink-sdk')
        (self.root/'freeink-sdk').symlink_to(self.root.parent)
        with self.assertRaises(RuntimeError):self.repair()

    def test_rejects_escaping_environment(self):
        for environment in ('','..','../elsewhere','a/b'):
            with self.assertRaises(RuntimeError):self.repair(environment)

    def test_missing_environment_needs_no_repair(self):self.assertEqual(self.repair('unbuilt'),0)


if __name__=='__main__':unittest.main()
