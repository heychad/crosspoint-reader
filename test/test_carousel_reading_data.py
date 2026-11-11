"""Exercise the Carousel's read-only per-book data loading."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
home = (ROOT / 'src/activities/home/HomeActivity.cpp').read_text()
start = home.index('static HomeDashboardData loadCarouselBookData(')
end = home.index('\n#endif', start)
program = r'''
#include <cassert>
#include <memory>
#include <string>
#include "components/themes/minimal/HomeDashboardData.h"
struct Settings {bool trackReadingStats=true;} SETTINGS;
namespace FsHelpers {
bool hasEpubExtension(const std::string& p){return p=="a.epub";}
bool hasXtcExtension(const std::string& p){return p=="b.xtc";}
}
int progressReads=0; int loadBookProgress(const std::string&){++progressReads;return 45;}
struct Epub {static std::string cachePathForFilePath(const std::string&,const char*){return "epub";}};
struct Xtc {Xtc(const std::string&,const char*){} std::string getCachePath()const{return "xtc";}};
struct BookReadingStats {enum class LoadMode {ReadOnly}; bool writable=true; unsigned totalReadingSeconds=120; int sessionCount=1;};
bool allocationFails=false;
template<class T,class... A> std::unique_ptr<T> makeUniqueNoThrow(A&&... a){if(allocationFails)return {};return std::make_unique<T>(static_cast<A&&>(a)...);}
namespace readingstats {
bool enabled=true, found=true, writable=true; int sessions=1, reads=0; std::string last;
BookReadingStats loadBook(const std::string& cache,const std::string&,bool& e,bool* f,BookReadingStats::LoadMode mode){
 assert(mode==BookReadingStats::LoadMode::ReadOnly);++reads;last=cache;e=enabled;*f=found;
 BookReadingStats s;s.writable=writable;s.sessionCount=sessions;return s;}
}
''' + home[start:end] + r'''
int main(){
 auto a=loadCarouselBookData("a.epub");assert(a.progress==45 && a.hasStats && a.readingSeconds==120 && readingstats::last=="epub");
 auto b=loadCarouselBookData("b.xtc");assert(b.progress==45 && b.hasStats && readingstats::last=="xtc");
 SETTINGS.trackReadingStats=false;int reads=readingstats::reads;a=loadCarouselBookData("a.epub");assert(a.progress==45 && !a.hasStats && readingstats::reads==reads);
 SETTINGS.trackReadingStats=true;readingstats::sessions=0;a=loadCarouselBookData("a.epub");assert(!a.hasStats && a.progress==45);
 readingstats::sessions=1;readingstats::found=false;a=loadCarouselBookData("a.epub");assert(!a.hasStats);
 readingstats::found=true;readingstats::writable=false;a=loadCarouselBookData("a.epub");assert(!a.hasStats);
 readingstats::writable=true;allocationFails=true;a=loadCarouselBookData("a.epub");assert(!a.hasStats && a.progress==45);
 allocationFails=false;int before=progressReads;a=loadCarouselBookData("a.md");assert(!a.hasStats && a.progress==-1 && before==progressReads);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    source = Path(tmp) / 'test.cpp'
    binary = Path(tmp) / 'test'
    source.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Carousel saved progress and reading time loaded read-only')
