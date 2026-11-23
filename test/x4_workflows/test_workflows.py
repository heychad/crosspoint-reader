"""Verify X4 Pro workflow matrices and run release safeguards without GitHub."""
import configparser
import hashlib
import os
from pathlib import Path
import re
import shutil
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
assert re.findall(r'^\s*- pio_env:\s*(\S+)', rc_matrix, re.M) == ['x4pro-chad-test']
assert re.findall(r"name: 'firmware[^']*'", links) == ["name: 'firmware-x4pro.bin'"]
assert 'pio check -e x4pro' in ci
assert 'python3 scripts/gen_i18n.py' in ci
assert 'ctest --test-dir build/test --output-on-failure -j 1' in ci
assert 'if: ${{ !github.event.release.prerelease }}' in release
assert 'ref: ${{ github.sha }}' in release
config = configparser.ConfigParser(interpolation=None)
config.read(ROOT / 'platformio.ini')
feature_section = '${x4pro_personal_features.build_flags}'
features = set(config['x4pro_personal_features']['build_flags'].split())
assert features == {'-DCROSSPOINT_EXPERIMENTAL_' + feature + '=1' for feature in (
    'COMPLETION', 'COMPLETION_PROMPT', 'STATS_RESTORE', 'DAILY_STATS', 'PAGE_OVERLAY', 'CAROUSEL',
    'COMPLETION_MOVE', 'CHAPTER_TIME', 'DEFERRED_AUTOMATION', 'STATS_BACKUP', 'CHAPTER_PREFETCH', 'PERSONAL_UPDATES')}
for profile in ('x4pro-chad-test', 'x4pro-gh_release'):
    assert feature_section in config[f'env:{profile}']['build_flags']
assert feature_section not in config['env:x4pro']['build_flags']

validate = run_block(release, 'Validate release version')
prepare_release = run_block(release, 'Prepare firmware asset')
attach = run_block(release, 'Attach firmware assets')
validate_test = run_block(rc, 'Validate test version')
verify_candidate = run_block(rc, 'Verify and name candidate asset')

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
    bin_dir = root / 'bin'
    bin_dir.mkdir()
    gh = bin_dir / 'gh'
    gh.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "$GH_RECORD"\n')
    gh.chmod(0o755)
    record = root / 'gh-args'
    args = {'PATH': str(bin_dir) + os.pathsep + env['PATH'], 'GH_RECORD': str(record), 'GITHUB_REPOSITORY': 'heychad/crosspoint-reader'}
    assert run(attach, **args).returncode == 0
    called = record.read_text().splitlines()
    assert called == ['release', 'upload', '2.0.0-chad.4', str(asset.relative_to(root)), '--repo', 'heychad/crosspoint-reader']
    record.unlink()
    (root / 'dist/crosspoint-2.0.0-chad.4-x4.bin').write_bytes(b'extra')
    assert run(attach, **args).returncode != 0 and not record.exists()
    (root / 'dist/crosspoint-2.0.0-chad.4-x4.bin').unlink()
    asset.unlink()
    assert run(attach, **args).returncode != 0 and not record.exists()

    (root / 'platformio.ini').write_text('version = 2.0.0-chad.4-test1-previous\n')
    source = '412365e1' * 5
    sdk = 'abda5534' * 5
    github_env = root / 'github-env'
    test_env = {'GITHUB_ENV': str(github_env), 'GITHUB_SHA': source}
    version = '2.0.0-chad.4-test2-new-fix'
    assert run(validate_test, TEST_VERSION=version, **test_env).returncode == 0
    assert github_env.read_text().splitlines() == [f'CROSSPOINT_X4PRO_TEST_VERSION={version}', f'SHORT_SHA={source[:12]}']
    github_env.unlink()
    for invalid in ('2.0.0-chad.3-test2', '2.0.0-chad.4', '2.0.0-chad.4-test2-x4pro',
                    '2.0.0-chad.4-test2-foo\nBAR=yes', '2.0.0-chad.4-test2-A',
                    '2.0.0-chad.4-test2--fix', '2.0.0-chad.4-test2-' + 'a' * 62):
        assert run(validate_test, TEST_VERSION=invalid, **test_env).returncode != 0, invalid
        assert not github_env.exists()

    git = bin_dir / 'git'
    git.write_text('''#!/bin/sh
case "$*" in
    'rev-parse HEAD') printf '%s\\n' "$SOURCE_REVISION" ;;
    'rev-parse HEAD:freeink-sdk') printf '%s\\n' "$SDK_PIN" ;;
    '-C freeink-sdk rev-parse HEAD') printf '%s\\n' "$SDK_CHECKOUT" ;;
    *) exit 1 ;;
esac
''')
    git.chmod(0o755)
    profile = 'x4pro-chad-test'
    (root / 'partitions.csv').write_bytes((ROOT / 'partitions.csv').read_bytes())
    (root / 'scripts').mkdir()
    shutil.copyfile(ROOT / 'scripts/check_x4pro_firmware.py', root / 'scripts/check_x4pro_firmware.py')
    build = root / '.pio/build' / profile
    build.mkdir(parents=True)
    firmware = build / 'firmware.bin'
    asset_name = 'firmware-x4pro.bin'
    candidate_env = {'PATH': args['PATH'], 'PIO_ENV': profile, 'ASSET': asset_name,
                     'GITHUB_SHA': source, 'SOURCE_REVISION': source, 'SDK_PIN': sdk, 'SDK_CHECKOUT': sdk,
                     'CROSSPOINT_X4PRO_TEST_VERSION': version}
    board = b'CROSSPOINT-BOARD-V1:x4pro;'
    image = bytearray(24)
    image[0] = 0xe9
    image[12] = 9
    image += board + b'\x00' + (version + '-x4pro').encode() + b'\x00'
    firmware.write_bytes(image)
    assert run(verify_candidate, **candidate_env).returncode == 0
    assert (build / asset_name).read_bytes() == image
    assert (build / 'SHA256SUMS').read_text() == f'{hashlib.sha256(image).hexdigest()}  {asset_name}\n'
    assert (build / 'SOURCE.txt').read_text().splitlines() == [
        f'Commit: {source}', f'FreeInk SDK: {sdk}', f'Profile: {profile}', f'Version: {version}-x4pro']

    stable_profile = 'x4pro-gh_release'
    stable_build = root / '.pio/build' / stable_profile
    stable_build.mkdir()
    stable_firmware = stable_build / 'firmware.bin'
    stable_asset = root / 'crosspoint-2.0.0-chad.4-x4pro.bin'
    stable_image = image.replace((version + '-x4pro').encode(), b'2.0.0-chad.4')
    github_output = root / 'github-output'
    stable_env = candidate_env | {'PIO_ENV': stable_profile, 'GITHUB_OUTPUT': str(github_output)}
    for invalid_stable in (image, stable_image.replace(b'2.0.0-chad.4', b'12.0.0-chad.4'),
                           stable_image.replace(board, b'CROSSPOINT-BOARD-V1:x4;')):
        stable_firmware.write_bytes(invalid_stable)
        assert run(prepare_release, **stable_env).returncode != 0
        assert not stable_asset.exists() and not github_output.exists()
    stable_firmware.write_bytes(stable_image)
    assert run(prepare_release, **(stable_env | {'GITHUB_SHA': sdk})).returncode != 0
    assert not stable_asset.exists() and not github_output.exists()
    assert run(prepare_release, **stable_env).returncode == 0
    assert stable_asset.read_bytes() == stable_image
    assert github_output.read_text() == f'name={stable_asset.name}\n'

    (build / asset_name).unlink()
    assert run(verify_candidate, **(candidate_env | {'SDK_CHECKOUT': source})).returncode != 0
    assert run(verify_candidate, **(candidate_env | {'GITHUB_SHA': sdk})).returncode != 0
    for invalid_image in (image[:13], image[:12] + b'\x05' + image[13:],
                          image.replace(board, b'CROSSPOINT-BOARD-V1:x4;'),
                          image + b'CROSSPOINT-BOARD-V1:x4;', image.replace(version.encode(), b'old')):
        firmware.write_bytes(invalid_image)
        assert run(verify_candidate, **candidate_env).returncode != 0
        assert not (build / asset_name).exists()
    firmware.write_bytes(image)
    (root / 'partitions.csv').write_text('app0,app,ota_0,0x10000,0x20,\napp1,app,ota_1,0x20000,0x1000,\n')
    assert run(verify_candidate, **candidate_env).returncode != 0
    assert not (build / asset_name).exists()
print('X4-only workflows, staged candidate identity, and no-overwrite release guards passed')
