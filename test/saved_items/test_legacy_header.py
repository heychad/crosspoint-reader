"""Exercise the production header for a partially unreadable legacy list."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/reader/CrossInkBookmarksActivity.cpp').read_text()
start = source.index('const char* CrossInkBookmarksActivity::headerTitle() const {')
end = source.index('\n}', start) + 2
method = source[start:end]
code = r'''
#include <cassert>
#include <string>
constexpr int STR_SAVED_ITEMS_ERROR=1, STR_CROSSINK_BOOKMARKS=2, STR_SAVED_ITEMS_INCOMPLETE=3;
const char* tr(int id){return id==1?"error":id==2?"bookmarks":"incomplete";}
struct Bookmarks {bool bad=false;int entries=0;bool unreadable()const{return bad;}int count()const{return entries;}};
struct CrossInkBookmarksActivity {Bookmarks bookmarks;const char* headerTitle()const;};
''' + method + r'''
int main(){CrossInkBookmarksActivity screen;
 assert(std::string(screen.headerTitle())=="bookmarks");
 screen.bookmarks.bad=true;assert(std::string(screen.headerTitle())=="error");
 screen.bookmarks.entries=1;assert(std::string(screen.headerTitle())=="incomplete");}
'''
with tempfile.TemporaryDirectory() as folder:
    src = Path(folder) / 'header.cpp'
    executable = Path(folder) / 'header'
    src.write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(src), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Legacy bookmark error header passed')
