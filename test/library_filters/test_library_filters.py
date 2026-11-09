"""Exercise production filtering and pinned-row mappings over an in-memory index."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/library/LibraryListActivity.cpp').read_text()


def method(signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


# The layout fixture uses the base's zero max-padding override.
assert 'tabPillMaxPad' not in method('LibraryListActivity::LibraryListActivity(')
code = (ROOT / 'test/library_filters/LibraryFiltersHarness.cpp').read_text()
methods = []
for signature in ('int LibraryListActivity::pinnedCount(', 'void LibraryListActivity::resolvePinned(',
                  'void LibraryListActivity::refreshOverlap(', 'bool LibraryListActivity::filtersActive(',
                  'bool LibraryListActivity::matchesFilters(', 'bool LibraryListActivity::matchesFormat(',
                  'void LibraryListActivity::applyFilter(', 'bool LibraryListActivity::rowMetadataFor(',
                  'bool LibraryListActivity::buildGroupStarts(', 'void LibraryListActivity::formatMetadataHeading(',
                  'int LibraryListActivity::bookRowCount(', 'int LibraryListActivity::rowFor(',
                  'bool LibraryListActivity::rowPathFor(', 'bool LibraryListActivity::gridRequested(',
                  'bool LibraryListActivity::gridEnabled(', 'int LibraryListActivity::navigationPageSize(',
                  'bool LibraryListActivity::handleGridSwipe(',
                  'bool LibraryListActivity::buildGrid(',
                  'void LibraryListActivity::loadNextGridCover(',
                  'void LibraryListActivity::buildRows('):
    methods.append(method(signature))
code = code.replace('// INSERT_METHODS', '\n'.join(methods))
with tempfile.TemporaryDirectory() as temporary:
    directory=Path(temporary)
    (directory/'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',*shlex.split(os.environ.get('CXXFLAGS','')),
                    '-I',str(ROOT/'lib/LibraryIndex'),str(directory/'test.cpp'),'-o',str(directory/'test')],check=True)
    subprocess.run([str(directory/'test')],check=True)
print('Library filters, metadata groups/search, pins, failures and 100 allocation-free warm paints passed')
