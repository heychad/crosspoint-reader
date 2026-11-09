"""Exercise the real EPUB start-of-page progress calculation used by the bar."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = source.index('int EpubReaderActivity::getProgressBasisPoints() const {')
end = source.index('\n}\n', start) + 2
code = r'''
#include <algorithm>
struct Section { int currentPage = 0, pages = 10; int estimatedTotalPages() const { return pages; } };
struct Epub { int getBookSize() const { return 1; } float calculateProgress(int, float p) const { return p; } };
struct EpubReaderActivity {
  Section* section; Epub* epub; int currentSpineIndex = 0; bool end = false;
  bool isAtEndOfBook() const { return end; }
  int getProgressPercent() const { return 50; }
  int getProgressBasisPoints() const;
};
''' + source[start:end] + r'''
int main() {
  Section section; Epub epub; EpubReaderActivity reader{&section, &epub};
  if (reader.getProgressBasisPoints() != 0) return 1;
  section.currentPage = 9;
  if (reader.getProgressBasisPoints() != 9000) return 2;
  section.pages = 1; section.currentPage = 0;
  if (reader.getProgressBasisPoints() != 0) return 3;
  reader.end = true;
  if (reader.getProgressBasisPoints() != 10000) return 4;
  return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    temporary = Path(directory)
    (temporary / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(temporary / 'test.cpp'), '-o', str(temporary / 'test')], check=True)
    subprocess.run([str(temporary / 'test')], check=True)
print('Time-left progress keeps the current page remaining')
