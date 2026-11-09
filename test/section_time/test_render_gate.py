"""Exercise the production guard for displaying section estimates."""
from pathlib import Path
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
source=(root/'src/activities/reader/EpubReaderActivity.cpp').read_text()
start=source.index('    if (wantsSectionTime && section')
end=source.index('\n    readerbars::draw(',start)
body=source[start:end]
code=r'''
#include <cassert>
#include <cstddef>
#include <cstdint>
struct Section {bool building=false,partial=false;int pageCount=10;bool isBuilding(){return building;}bool isPartial(){return partial;}};
struct Stats {int calls=0;uint32_t sectionTimeLeft(int page,int total){++calls;assert(page==2&&total==10);return 42;}};
struct Reader {bool wantsSectionTime=true;Section* section=nullptr;Stats readingStats;int currentPage=2;char sectionTime[32]={};int formats=0;
 void formatTime(uint32_t n,char*,size_t){assert(n==42);++formats;}
 void render(){
''' + body + r'''
 }};
int main(){Reader r;r.render();assert(!r.formats);Section s;r.section=&s;
 s.building=true;r.render();assert(!r.formats);s.building=false;s.partial=true;r.render();assert(!r.formats);
 s.partial=false;r.wantsSectionTime=false;r.render();assert(!r.formats);
 r.wantsSectionTime=true;r.render();assert(r.formats==1&&r.readingStats.calls==1);}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);(tmp/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
    subprocess.run([str(tmp/'test')],check=True)
print('Section-time rendering excludes incomplete layouts')
