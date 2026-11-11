"""Check statistics row routing for device and aggregate frequency rows."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/settings/ReadingStatsActivity.cpp').read_text()
functions = source[source.index('int ReadingStatsActivity::listCount()'):source.index('void ReadingStatsActivity::provideRow(')]
code = r'''
#include <cassert>
#include <cstddef>
#include <iterator>
class ReadingStatsActivity {
 public:
  enum class Tab {Book, Device, AllDevices};
  enum class Row {Title, Time, Charts, RecordedWeek, RecordedMonth, RecordedYear, BackupDevice,
                  Morning, Afternoon, Evening, Night, Monday, Tuesday, Wednesday,
                  Thursday, Friday, Saturday, Sunday};
  inline static constexpr Row BOOK_ROWS[] = {Row::Title, Row::Time, Row::Charts};
  inline static constexpr Row GLOBAL_ROWS[] = {Row::Time, Row::Charts};
  Tab tab = Tab::Book;
  bool bookTab() const { return tab == Tab::Book; }
  int listCount() const;
  Row rowAt(int) const;
};
''' + functions + r'''
int main() {
  using A = ReadingStatsActivity;
  A a;
  assert(a.listCount() == 14 && a.rowAt(3) == A::Row::Morning && a.rowAt(13) == A::Row::Sunday);
  a.tab = A::Tab::Device;
  assert(a.listCount() == 17 && a.rowAt(2) == A::Row::RecordedWeek && a.rowAt(3) == A::Row::RecordedMonth);
  assert(a.rowAt(4) == A::Row::RecordedYear && a.rowAt(5) == A::Row::BackupDevice && a.rowAt(6) == A::Row::Morning && a.rowAt(16) == A::Row::Sunday);
  a.tab = A::Tab::AllDevices;
  assert(a.listCount() == 16 && a.rowAt(2) == A::Row::RecordedWeek && a.rowAt(3) == A::Row::RecordedMonth);
  assert(a.rowAt(4) == A::Row::RecordedYear && a.rowAt(5) == A::Row::Morning && a.rowAt(15) == A::Row::Sunday);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
print('Statistics row routing passed')
