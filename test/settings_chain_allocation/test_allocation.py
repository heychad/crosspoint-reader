"""Exercise checked chain allocation and file-identity rules."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory() as directory:
    executable = Path(directory) / 'test'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    '-I', str(root / 'test/settings_recovery_plan/stubs'),
                    '-I', str(root / 'test/book_reading_stats_atomic/stubs'),
                    '-I', str(root / 'lib/hal'),
                    str(root / 'test/settings_chain_allocation/SettingsChainAllocationTest.cpp'),
                    '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Checked chain allocation and file identity passed')
