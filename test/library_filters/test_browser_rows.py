"""Check production File Browser row values and line-count preferences."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/home/FileBrowserActivity.cpp').read_text()


def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


layout_start = source.index('  fui::TextStyle label = screen.theme().smallText;')
layout_end = source.index('  props.labelText = label;', layout_start) + len('  props.labelText = label;')
code = (ROOT / 'test/library_filters/BrowserRowsHarness.cpp').read_text()
code = code.replace('// INSERT_LAYOUT', source[layout_start:layout_end])
methods = []
for signature in ('void formatFileName(', 'void formatFileExtension(', 'void FileBrowserActivity::provideRow('):
    methods.append(method(signature))
code = code.replace('// INSERT_METHODS', '\n'.join(methods))
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(directory / 'test.cpp'), '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test')], check=True)
print('File Browser extension visibility, row reuse, line counts and firmware-picker isolation passed')
