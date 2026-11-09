"""Run production cover-grid methods with the real UI layout and storage doubles."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'src/components/LibraryCoverGrid.h').read_text()
source = (ROOT / 'src/components/LibraryCoverGrid.cpp').read_text()
header = '\n'.join(line for line in header.splitlines() if not line.startswith(('#include', '#pragma')))
source = '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))
code = (ROOT / 'test/recent_grid/GridHarness.cpp').read_text()
code = code.replace('// INSERT_GRID_HEADER', header).replace('// INSERT_GRID_SOURCE', source)
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    (directory / 'grid.cpp').write_text(code)
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
               '-I', str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'),
               '-I', str(ROOT / 'lib/Utf8'), '-I', str(ROOT / 'lib/EpdFont'), str(directory / 'grid.cpp'),
               str(ROOT / 'lib/EpdFont/EpdFont.cpp'),
               str(ROOT / 'lib/Utf8/Utf8.cpp'),
               str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'),
               '-o', str(directory / 'grid')]
    subprocess.run(command, check=True)
    subprocess.run([str(directory / 'grid')], check=True)
print('Production grid layout, cell identity, thumbnail failure and 100 allocation-free warm draws passed')
