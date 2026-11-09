"""Render production chart builders with native SDK layout and built-in fonts."""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/settings/ReadingChartsActivity.cpp').read_text()
assert 'UITheme::getInstance().getHeaderRect(renderer, true, false)' in source

def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

methods = '\n'.join(method(source, name) for name in [
    'void ReadingChartsActivity::build(', 'void ReadingChartsActivity::drawBars(',
    'void ReadingChartsActivity::drawCalendar('])
labels = source[source.index('namespace {'):source.index('ReadingChartsActivity::ReadingChartsActivity')]
translations = []
available = set(re.findall(r"\bSTR_\w+", (ROOT / 'lib/I18n/I18nKeys.h').read_text()))
for name, literal in re.findall(r'^(STR_\w+): (".*")$', (ROOT / 'lib/I18n/translations/english.yaml').read_text(), re.M):
    if name not in available:
        continue
    translations.append(f'case StrId::{name}: return {json.dumps(json.loads(literal))};')
lookup = 'const char* tr(StrId id) { switch(id) {' + ''.join(translations) + 'default:return "";}}'
duration = method((ROOT / 'src/activities/reader/BookReadingStats.cpp').read_text(), 'void BookReadingStats::formatDuration(')

metric_names = ['topPadding', 'headerHeight', 'tabBarHeight', 'verticalSpacing', 'contentSidePadding', 'menuRowHeight', 'buttonHintsHeight']
profiles = []
base_metrics = {}
for header in ['BaseTheme.h', 'lyra/LyraTheme.h', 'roundedraff/RoundedRaffTheme.h']:
    content = (ROOT / 'src/components/themes' / header).read_text()
    values = dict(base_metrics)
    values.update({name: int(value) for name, value in re.findall(r'\.(\w+)\s*=\s*(\d+)', content)})
    if not base_metrics:
        base_metrics = values
    profiles.append('Metrics{' + ','.join(str(values[name]) for name in metric_names) + '}')
metrics = 'const std::array<Metrics,3> profiles{{' + ','.join(profiles) + '}};'

code = (ROOT / 'test/reading_charts/ChartsUiHarness.cpp').read_text()
code = code.replace('// METRICS', metrics)
code = code.replace('// TRANSLATIONS', lookup).replace('// LABELS', labels).replace('// METHODS', methods).replace('// DURATION', duration)
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I' + str(ROOT / 'src'), '-I' + str(ROOT / 'lib/EpdFont'), '-I' + str(ROOT / 'lib/Utf8'),
                    '-I' + str(ROOT / 'lib/I18n'), '-I' + str(ROOT / 'test/book_reading_stats_atomic/stubs'),
                    '-I' + str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/include'), str(path / 'test.cpp'),
                    str(ROOT / 'src/activities/reader/ReadingStatsUtils.cpp'), str(ROOT / 'lib/EpdFont/EpdFont.cpp'),
                    str(ROOT / 'lib/Utf8/Utf8.cpp'), str(ROOT / 'freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp'),
                    '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Chart UI passed three production theme profiles, four X4 Pro orientations, two fonts and all warning states')
