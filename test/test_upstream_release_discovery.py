"""Check stable/RC discovery against two remotes with colliding tag names."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('snapshot', root / 'scripts/upstream_snapshot.py')
snapshot = importlib.util.module_from_spec(spec)
spec.loader.exec_module(snapshot)


def repository(path, tags):
    def git(*args):
        return subprocess.check_output(['git', '-C', str(path), *args], text=True).strip()
    path.mkdir()
    git('init', '--quiet')
    git('config', 'user.name', 'Test')
    git('config', 'user.email', 'example@localhost')
    git('-c', 'commit.gpgsign=false', 'commit', '--allow-empty', '-qm', path.name)
    for tag in tags:
        git('-c', 'tag.gpgsign=false', 'tag', '-a', tag, '-m', tag)
    return git('rev-parse', 'HEAD')


class UpstreamReleaseDiscovery(unittest.TestCase):
    def test_release_order(self):
        names = ['v1.6.5', '2.0.0rc', 'v2.0.0-rc-2', 'v2.0.0-rc10', '2.0.0', '2.1.0-rc.1']
        self.assertEqual(sorted(reversed(names), key=snapshot.release_order), names)
        for other in ('nightly', '2.0.0-dev8', 'v2.1.0-beta1', 'v2.1.0-rc1-extra'):
            self.assertIsNone(snapshot.release_order(other))

    def test_remote_ownership_and_peeled_commits(self):
        with tempfile.TemporaryDirectory() as tmp:
            a, b = Path(tmp) / 'a', Path(tmp) / 'b'
            tip_a = repository(a, ['2.0.0', '2.1.0-rc2', '2.1.0-rc10', 'nightly'])
            tip_b = repository(b, ['2.0.0', 'v1.6.1'])
            found = snapshot.advertised_releases(str(a), 'refs/tags/2.0.0', tip_a)
            self.assertEqual(found['latest_stable'], '2.0.0')
            self.assertEqual(found['latest_rc'], '2.1.0-rc10')
            self.assertEqual(found['newer_tags'], ['2.1.0-rc10', '2.1.0-rc2'])
            self.assertTrue(found['selected_commit_matches'])
            nightly = snapshot.advertised_releases(str(a), 'refs/tags/nightly', tip_a)
            self.assertTrue(nightly['selected_commit_matches'])
            self.assertEqual(nightly['newer_tags'], [])
            other = snapshot.advertised_releases(str(b), 'refs/tags/2.0.0', tip_a)
            self.assertFalse(other['selected_commit_matches'])
            self.assertEqual(other['newer_tags'], [])
            absent = snapshot.advertised_releases(str(b), 'refs/tags/2.1.0-rc2', tip_b)
            self.assertFalse(absent['selected_tag_available'])
            self.assertIsNone(absent['selected_commit_matches'])


if __name__ == '__main__':
    unittest.main()
