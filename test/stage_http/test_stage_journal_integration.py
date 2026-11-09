"""Exercise the existing journal with local staging and verification handlers."""
import hashlib
import os
from pathlib import Path
import runpy
import select
import shlex
import subprocess
import sys
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
BRAIN = Path(os.environ.get('CROSSPOINT_SECOND_BRAIN', str(Path.home() / 'second-brain')))
if not (BRAIN / 'scripts/wireless_stage.py').is_file():
    print('Second-brain journal sources are unavailable; journal integration skipped')
    raise SystemExit(77)
sys.path[:0] = [str(BRAIN / 'scripts'), str(ROOT / 'scripts')]
from wireless_books import WirelessReader
from wireless_raw_stage import upload_stage
from wireless_stage import read_stage_journal, stage_one

fixture = runpy.run_path(str(ROOT / 'test/stage_http/test_raw_to_hash_integration.py'))
data = bytes((i * 37 + i // 251) % 251 for i in range(9000))
digest = hashlib.sha256(data).hexdigest()
name = '__xsync_0123456789abcdef0123456789abcdef.part'

with tempfile.TemporaryDirectory() as tmp:
    directory = Path(tmp)
    (directory / 'HalStorage.h').write_text(fixture['fixture']['fixture']['storage'])
    (directory / 'mbedtls').mkdir()
    (directory / 'mbedtls/sha256.h').write_text(fixture['fixture']['sha_shim'])
    (directory / 'server.cpp').write_text(fixture['code'])
    executable = directory / 'server'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', tmp,
                    '-I', str(ROOT / 'src/network/sync'), str(directory / 'server.cpp'), *fixture['crypto_flags'],
                    '-o', str(executable)], check=True, timeout=30)
    source = directory / 'books'
    source.mkdir()
    (source / 'book.epub').write_bytes(data)
    for lose_confirmation in (False, True):
        server = subprocess.Popen([str(executable)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            if not select.select([server.stdout], [], [], 5)[0]:
                raise TimeoutError('Local stage and verification server did not start')
            raw_port, hash_port = map(int, server.stdout.readline().split())
            reader = WirelessReader(f'http://127.0.0.1:{hash_port}')
            reader.checked_scans = True
            reader.stage_version = 1
            reader.stage_hash_version = 2
            reader.reader_id = 'a' * 64
            reader.confirm_reader = lambda: None  # Device identity is not implemented in this fixture.
            attempts = []

            def upload(stage, stream, size, checksum):
                attempts.append(stage)
                upload_stage('http://127.0.0.1', raw_port, stage, reader.reader_id, stream, size, checksum)
                if lose_confirmation:
                    raise OSError('simulated lost upload confirmation')

            reader.upload_stage = upload
            reports = directory / ('lost' if lose_confirmation else 'normal')
            with patch('wireless_stage.secrets.token_hex', return_value='0123456789abcdef0123456789abcdef'):
                try:
                    result = stage_one(reader, source, {'relative': 'book.epub', 'size': len(data), 'sha256': digest},
                                       reports, reader_mount=directory / 'reader')
                    assert not lose_confirmation
                    journal = read_stage_journal(result['journal'])
                    assert journal['events'] == ('prepared', 'uploaded', 'verified')
                except OSError:
                    if not lose_confirmation:
                        raise
                    paths = list(reports.glob('*.jsonl'))
                    assert len(paths) == 1
                    journal = read_stage_journal(paths[0])
                    assert journal['events'] == ('prepared', 'stopped')
                    assert reader.hash_stage(journal['stage'], journal['size']) == digest
                    assert read_stage_journal(paths[0])['events'] == ('prepared', 'stopped')
            assert attempts == [name]
            _, errors = server.communicate(timeout=5)
            assert server.returncode == 0, errors
        except BaseException:
            if server.poll() is None:
                server.terminate()
            _, errors = server.communicate(timeout=5)
            if errors:
                print('Local server error:', errors, file=sys.stderr)
            raise
        finally:
            if server.poll() is None:
                server.terminate()
                server.wait(timeout=5)

print('Journal, local stage and SHA-256 verification passed; lost confirmation did not retry')
