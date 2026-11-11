"""Verify X4 Pro workflow matrices and run release safeguards without GitHub."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import textwrap

ROOT = Path(__file__).resolve().parents[2]
WORKFLOWS = ROOT / '.github/workflows'


def run_block(text, name):
    section = text.split(f'      - name: {name}\n', 1)[1]
    lines = section.splitlines()
    start = next(i for i, line in enumerate(lines) if line == '        run: |') + 1
    block = []
    for line in lines[start:]:
        if line and not line.startswith('          '):
            break
        block.append(line)
    return textwrap.dedent('\n'.join(block)) + '\n'


ci = (WORKFLOWS / 'ci.yml').read_text()
release = (WORKFLOWS / 'release.yml').read_text()
rc = (WORKFLOWS / 'release_candidate.yml').read_text()
links = (WORKFLOWS / 'pr-firmware-links.yml').read_text()
ci_matrix = ci.split('      matrix:\n', 1)[1].split('    steps:\n', 1)[0]
release_matrix = release.split('      matrix:\n', 1)[1].split('    steps:\n', 1)[0]
rc_matrix = rc.split('      matrix:\n', 1)[1].split('    steps:\n', 1)[0]
assert re.findall(r'^\s*- env:\s*(\S+)', ci_matrix, re.M) == ['x4pro']
assert re.findall(r'^\s*- release_env:\s*(\S+)', release_matrix, re.M) == ['x4pro-gh_release']
assert re.findall(r'^\s*- pio_env:\s*(\S+)', rc_matrix, re.M) == ['x4pro-gh_release_rc']
assert re.findall(r"name: 'firmware[^']*'", links) == ["name: 'firmware-x4pro.bin'"]
assert 'pio check -e x4pro' in ci
assert 'python3 scripts/gen_i18n.py' in ci
assert 'ctest --test-dir build/test --output-on-failure -j 1' in ci
assert 'if: ${{ !github.event.release.prerelease }}' in release
for step in ('Prepare license asset', 'Stage license asset'):
    section = release.split(f'      - name: {step}\n', 1)[1].split('      - ', 1)[0]
    assert '        if:' not in section, 'X4 Pro release must always include its license'
validate = run_block(release, 'Validate release version')
attach = run_block(release, 'Attach firmware assets')

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    (root / 'platformio.ini').write_text('version = 2.0.0-chad.4\n')
    env = dict(os.environ, RELEASE_TAG='2.0.0-chad.4')
    def run(script, **overrides):
        return subprocess.run(['bash', '-c', script], cwd=root, env=env | overrides, capture_output=True, text=True)
    assert run(validate).returncode == 0
    assert run(validate, RELEASE_TAG='2.0.0-chad.3').returncode != 0
    for invalid in ('v2.0.0-chad.4', '2.0.0', '2.0.0-chad.4-test1',
                    '2.0.0-chad.4;touch BAD', '9999999999.0.0-chad.4', '999999999.999999999.1-chad.1'):
        (root / 'platformio.ini').write_text('version = ' + invalid + '\n')
        assert run(validate, RELEASE_TAG=invalid).returncode != 0, invalid
    (root / 'platformio.ini').write_text('version = 2.0.0-chad.4\n')
    (root / 'dist').mkdir()
    asset = root / 'dist/crosspoint-2.0.0-chad.4-x4pro.bin'
    asset.write_bytes(b'firmware fixture')
    (root / 'dist/LICENSE').write_text('License fixture')
    bin_dir = root / 'bin'
    bin_dir.mkdir()
    gh = bin_dir / 'gh'
    gh.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$GH_RECORD"\n')
    gh.chmod(0o755)
    record = root / 'gh-args'
    args = {'PATH': str(bin_dir) + os.pathsep + env['PATH'], 'GH_RECORD': str(record), 'GITHUB_REPOSITORY': 'heychad/crosspoint-reader'}
    assert run(attach, **args).returncode == 0
    called = record.read_text().splitlines()
    assert called == ['release', 'upload', '2.0.0-chad.4', str(asset.relative_to(root)), 'dist/LICENSE', '--repo', 'heychad/crosspoint-reader']
    record.unlink()
    (root / 'dist/crosspoint-2.0.0-chad.4-x4.bin').write_bytes(b'extra')
    assert run(attach, **args).returncode != 0 and not record.exists()
    (root / 'dist/crosspoint-2.0.0-chad.4-x4.bin').unlink()
    asset.unlink()
    assert run(attach, **args).returncode != 0 and not record.exists()
print('X4-only workflows and no-overwrite release guards passed')
