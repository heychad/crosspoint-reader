"""Exercise the reader's position snapshot before a failed cache is rebuilt."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
signature = 'void EpubReaderActivity::rememberFailedPageForRebuild()'
start = source.index(signature)
opening = source.index('{', start)
end, depth = opening + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[start:end]
load_failure = source.index('LOG_ERR("ERS", "Failed to load page from SD')
assert source.index('rememberFailedPageForRebuild();', load_failure) < source.index('section.reset();', load_failure)
code = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
struct Section {int currentPage=0;bool partial=false;};
struct EpubReaderActivity {
 std::unique_ptr<Section> section=std::make_unique<Section>();int nextPageNumber=2,currentSpineIndex=7,cachedSpineIndex=1;
 int cachedChapterTotalPageCount=50;std::optional<uint32_t> cachedVisibleTextOffset=123;
 void rememberFailedPageForRebuild();
};
''' + method + r'''
int main(){
 for(bool partial : {false,true}){EpubReaderActivity r;r.section->partial=partial;r.section->currentPage=17;
  r.rememberFailedPageForRebuild();assert(r.nextPageNumber==17 && r.cachedSpineIndex==7);
  assert(r.cachedChapterTotalPageCount==0 && !r.cachedVisibleTextOffset);}
 EpubReaderActivity absent;absent.section.reset();absent.rememberFailedPageForRebuild();assert(absent.nextPageNumber==2);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp, exe = Path(tmp) / 'test.cpp', Path(tmp) / 'test'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Cache rebuild keeps the failed page for complete and partial sections')
