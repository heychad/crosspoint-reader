"""A failed new thumbnail must not erase the saved cover path."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/activities/home/HomeActivity.cpp").read_text()
start = source.index("void HomeActivity::loadRecentCovers(int coverHeight)")
end = source.index("\nvoid HomeActivity::", start + 1)
function = source[start:end]
program = r'''
#include <cassert>
#include <string>
#include <vector>
struct Rect {};
struct RecentBook { std::string path,title,author,coverBmpPath; };
struct RecentBooksStore { int updates=0; void updateBook(const std::string&,const std::string&,const std::string&,const std::string&){++updates;} } RECENT_BOOKS;
struct StorageFake { bool exists(const char* p){return std::string(p).find("226")!=std::string::npos;} } Storage;
struct UITheme { static std::string getCoverThumbPath(const std::string& p,int h){return p+std::to_string(h);} };
namespace FsHelpers {
bool hasReflowableBookExtension(const std::string& p){return p=="book.epub";}
bool hasXtcExtension(const std::string& p){return p=="book.xtc";}
}
bool cacheLoads=true;
struct Epub { Epub(const std::string&,const char*){} bool load(bool,bool){return cacheLoads;} bool generateThumbBmp(int){return false;} bool generateThumbBmpFromSource(int){return false;} };
struct Xtc { Xtc(const std::string&,const char*){} bool load(){return true;} bool generateThumbBmp(int){return false;} };
constexpr int STR_LOADING_POPUP=1; const char* tr(int){return "Loading";}
struct Gui { template<class R> Rect drawPopup(R&,const char*){return{};} template<class R> void fillPopupProgress(R&,Rect,int){} } GUI;
struct Grid { int thumbHeightFor()const{return 300;} };
struct HomeActivity {
 bool recentsLoading=false,recentsLoaded=false,coverRendered=true; std::vector<RecentBook> recentBooks;
 Grid* coverGridUi=nullptr; int renderer=0, updates=0;
 void loadGridCover(RecentBook&,int,bool&,Rect&){}
 void requestUpdate(){++updates;}
 void loadRecentCovers(int);
};
''' + function + r'''
int main(){
 for(bool loads : {false,true}) for(const auto* name : {"book.epub", "book.xtc"}){
  cacheLoads=loads;
  RecentBook saved{name,"Title","Author","cover_[HEIGHT]"};
  HomeActivity minimal; minimal.recentBooks={saved}; minimal.loadRecentCovers(300);
  assert(minimal.recentsLoaded && !minimal.recentsLoading && minimal.recentBooks[0].coverBmpPath.empty());
  assert(RECENT_BOOKS.updates==0 && saved.coverBmpPath=="cover_[HEIGHT]");
  HomeActivity previous; previous.recentBooks={saved}; previous.loadRecentCovers(226);
  assert(previous.recentBooks[0].coverBmpPath==saved.coverBmpPath && RECENT_BOOKS.updates==0);
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(program)
    subprocess.run(["c++", "-std=c++20", str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("A failed thumbnail preserves the saved cover for other themes")
