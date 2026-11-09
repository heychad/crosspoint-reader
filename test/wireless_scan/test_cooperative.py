"""Run isolated directory decoding and cooperative-loop policy tests."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as directory:
    for source in ('policy_test.cpp', 'enumerator_test.cpp', 'cooperative_test.cpp'):
        executable = Path(directory) / source.removesuffix('.cpp')
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                        *shlex.split(os.environ.get('CXXFLAGS', '')), '-I', str(ROOT / 'src/network/sync'),
                        str(ROOT / 'test/wireless_scan' / source), '-o', str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print('Directory validation, enumeration, and cooperative policy passed')
