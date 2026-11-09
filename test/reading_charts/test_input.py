"""Exercise production chart navigation with a strict render-lock model."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/settings/ReadingChartsActivity.cpp').read_text()
start = source.index('void ReadingChartsActivity::changeView(')
end = source.index('void ReadingChartsActivity::buildScreen(', start)
code = (ROOT / 'test/reading_charts/InputHarness.cpp').read_text().replace('// METHODS', source[start:end])
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I' + str(ROOT / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Chart views, month bounds, touch/physical controls and routing gate passed')
