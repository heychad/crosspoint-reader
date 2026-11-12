"""Exercise dashboard data loading and display without a device."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
home = (root / 'src/activities/home/HomeActivity.cpp').read_text()
start = home.index('void HomeActivity::loadDashboardData()')
end = home.index('\nvoid HomeActivity::onEnter()', start)
load = home[start:end]
program = r'''
#include <cassert>
#include <memory>
#include <string>
#include <vector>
#include "components/themes/minimal/HomeDashboardData.h"
struct RecentBook {std::string path;};
struct Settings {bool trackReadingStats=true;} SETTINGS;
namespace FsHelpers {
bool hasEpubExtension(const std::string& p){return p=="book.epub";}
bool hasXtcExtension(const std::string& p){return p=="book.xtc";}
bool hasReflowableBookExtension(const std::string& p){return p=="book.epub" || p=="book.md";}
}
int progress=40,progressReads=0; int loadBookProgress(const std::string&){++progressReads;return progress;}
struct Epub {static std::string cachePathForFilePath(const std::string&,const char*){return "epub_cache";}};
struct Xtc {Xtc(const std::string&,const char*){} std::string getCachePath()const{return "xtc_cache";}};
struct BookReadingStats {
 enum class LoadMode {ReadOnly}; bool writable=true,isCompleted=false; unsigned totalReadingSeconds=100;
 unsigned remainingReadingSeconds(int p)const{return p==4000?123:0;}
};
bool allocationFails=false;
template<class T,class... Args> std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){if(allocationFails)return {};return std::make_unique<T>(static_cast<Args&&>(args)...);}
namespace readingstats {
bool enabled=true,found=true,writable=true,completed=false; int reads=0; std::string lastCache;
BookReadingStats loadBook(const std::string& cache,const std::string&,bool& e,bool* f,BookReadingStats::LoadMode mode){
 assert(mode==BookReadingStats::LoadMode::ReadOnly); ++reads;lastCache=cache;e=enabled;*f=found;
 BookReadingStats s;s.writable=writable;s.isCompleted=completed;return s;}
}
struct HomeActivity {HomeDashboardData dashboardData;std::vector<RecentBook> recentBooks;void loadDashboardData();};
''' + load + r'''
int main(){
 HomeActivity home;home.loadDashboardData();assert(home.dashboardData.progress==-1 && readingstats::reads==0);
 home.recentBooks={{"book.epub"}};SETTINGS.trackReadingStats=false;home.loadDashboardData();
 assert(home.dashboardData.progress==40 && !home.dashboardData.hasStats && readingstats::reads==0);
 SETTINGS.trackReadingStats=true;home.loadDashboardData();
 assert(home.dashboardData.hasStats && home.dashboardData.readingSeconds==100 && home.dashboardData.remainingSeconds==123 && readingstats::lastCache=="epub_cache");
 readingstats::enabled=false;home.loadDashboardData();assert(!home.dashboardData.hasStats && home.dashboardData.progress==40);
 readingstats::enabled=true;readingstats::writable=false;home.loadDashboardData();assert(!home.dashboardData.hasStats);
 readingstats::writable=true;readingstats::found=false;home.loadDashboardData();assert(!home.dashboardData.hasStats);
 readingstats::found=true;readingstats::completed=true;home.loadDashboardData();assert(home.dashboardData.hasStats && home.dashboardData.remainingSeconds==0);
 readingstats::completed=false;progress=-1;home.loadDashboardData();assert(home.dashboardData.hasStats && home.dashboardData.remainingSeconds==0);
 allocationFails=true;home.loadDashboardData();assert(!home.dashboardData.hasStats);
 allocationFails=false;progress=40;home.recentBooks={{"book.xtc"}};home.loadDashboardData();
 assert(home.dashboardData.hasStats && readingstats::lastCache=="xtc_cache");
 home.recentBooks={{"book.md"}};int before=progressReads;home.loadDashboardData();
 assert(progressReads==before && home.dashboardData.progress==-1 && home.dashboardData.hasStats && home.dashboardData.remainingSeconds==0);
 home.recentBooks={{"unsupported"}};home.loadDashboardData();
 assert(progressReads==before && home.dashboardData.progress==-1 && !home.dashboardData.hasStats);
}
'''

theme = (root / 'src/components/themes/minimal/DashboardHomeTheme.cpp').read_text()
geometry_start = theme.index('Rect DashboardHomeTheme::coverRect(Rect rect)')
geometry_end = theme.index('\nvoid DashboardHomeTheme::drawRecentBookCover(', geometry_start)
draw_start = theme.index('void DashboardHomeTheme::drawReadingInfo(')
render = theme[geometry_start:geometry_end] + '\n' + theme[draw_start:]
render_program = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "components/themes/minimal/HomeDashboardData.h"
struct Rect {int x,y,width,height;};
namespace MinimalHomeMetrics {struct M {int homeCoverHeight=300;};constexpr M values;}
enum class StrId {STR_STATS_PROGRESS,STR_STATS_TOTAL_TIME,STR_BAR_TIME_BOOK};
struct I18n { static I18n& getInstance(){static I18n value;return value;} const char* get(StrId x){return x==StrId::STR_STATS_PROGRESS?"Progress":x==StrId::STR_STATS_TOTAL_TIME?"Reading time":"Time left";} };
namespace EpdFontFamily {enum Style {REGULAR,BOLD};}
constexpr int UI_10_FONT_ID=10, UI_12_FONT_ID=12;
struct GfxRenderer {
 std::vector<std::string> values;
 std::string truncatedText(int,const char* p,int width,EpdFontFamily::Style=EpdFontFamily::REGULAR){assert(width>0);return p;}
 void drawText(int,int,int,const char* s,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR){values.emplace_back(s);}
 int getLineHeight(int){return 12;}
};
struct BookReadingStats {static void formatDuration(unsigned s,char* b,size_t n){snprintf(b,n,"%us",s);}};
struct DashboardHomeTheme {static Rect coverRect(Rect);static void drawReadingInfo(GfxRenderer&,Rect,const HomeDashboardData&);};
''' + render + r'''
int main(){
 Rect r{10,56,460,390}; GfxRenderer unknown; HomeDashboardData d;
 DashboardHomeTheme::drawReadingInfo(unknown,r,d);
 assert((unknown.values==std::vector<std::string>{"Progress","-","Reading time","-","Time left","-"}));
 d.progress=42;d.hasStats=true;d.readingSeconds=100;d.remainingSeconds=90;
 GfxRenderer known;DashboardHomeTheme::drawReadingInfo(known,r,d);
 assert((known.values==std::vector<std::string>{"Progress","42%","Reading time","100s","Time left","90s"}));
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    for name, contents in [('load', program), ('render', render_program)]:
        (directory / f'{name}.cpp').write_text(contents)
        subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(directory / f'{name}.cpp'), '-o', str(directory / name)], check=True)
        subprocess.run([str(directory / name)], check=True)
print('Dashboard loads saved values once, handles unavailable data, and draws them')

cover_start = theme.index('void DashboardHomeTheme::drawRecentBookCover(')
cover_end = theme.index('\nvoid DashboardHomeTheme::drawReadingInfo(', cover_start)
cover_program = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <string>
#include <vector>
struct Rect {int x,y,width,height;};
namespace MinimalHomeMetrics {struct M {int homeCoverHeight=300;};constexpr M values;}
struct RecentBook {std::string title,author,coverBmpPath;};
namespace EpdFontFamily {enum Style {REGULAR,BOLD};}
constexpr int UI_10_FONT_ID=10;
struct Scale {int bodyFontId=12;}; Scale uiScaleSpec(){return {};}
struct HalFile {bool close(){return true;}};
struct StorageFake {bool present=true;bool openFileForRead(const char*,const std::string&,HalFile&){return present;}} Storage;
enum class BmpReaderError {Ok,Bad};
struct Bitmap {explicit Bitmap(HalFile&){} BmpReaderError parseHeaders(){return BmpReaderError::Ok;}};
struct UITheme {static std::string getCoverThumbPath(const std::string& s,int){return s;}};
struct GfxRenderer {
 int selections=0,texts=0;Rect selected{};
 void drawRect(int x,int y,int w,int h,bool){++selections;selected={x,y,w,h};}
 std::string truncatedText(int,const char* s,int width,EpdFontFamily::Style=EpdFontFamily::REGULAR){assert(width>0);return s;}
 void drawCenteredText(int,int,const char*,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR){++texts;}
 int getLineHeight(int){return 12;}
};
struct DashboardHomeTheme {
 static Rect coverRect(Rect);
 mutable int placeholders=0,images=0,empty=0;
 void drawEmptyRecents(GfxRenderer&,Rect)const{++empty;}
 void drawCoverPlaceholder(GfxRenderer&,Rect)const{++placeholders;}
 bool drawCoverThumbFill(GfxRenderer&,Bitmap&,Rect)const{++images;return true;}
 void drawRecentBookCover(GfxRenderer&,Rect,const std::vector<RecentBook>&,int,bool&,bool&,bool&,std::function<bool()>)const;
};
''' + theme[geometry_start:geometry_end] + '\n' + theme[cover_start:cover_end] + r'''
int main(){
 DashboardHomeTheme theme; GfxRenderer renderer; bool rendered=false,stored=false,restored=false;int saves=0;
 std::vector<RecentBook> books{{"Title","Author","cover"}};Rect tile{10,56,460,390};
 auto save=[&]{++saves;return true;};
 theme.drawRecentBookCover(renderer,tile,books,0,rendered,stored,restored,save);
 assert(theme.images==1 && saves==1 && rendered && stored && renderer.selections==1 && renderer.texts==2);
 assert(renderer.selected.x>=tile.x && renderer.selected.x+renderer.selected.width<tile.x+tile.width/2);
 restored=true;theme.drawRecentBookCover(renderer,tile,books,1,rendered,stored,restored,save);
 assert(theme.images==1 && saves==1 && renderer.selections==1);
 restored=false;Storage.present=false;
 theme.drawRecentBookCover(renderer,tile,books,1,rendered,stored,restored,[&]{++saves;return false;});
 assert(theme.placeholders==1 && !rendered && !stored && saves==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / 'cover.cpp').write_text(cover_program)
    subprocess.run(['c++', '-std=c++20', str(directory / 'cover.cpp'), '-o', str(directory / 'cover')], check=True)
    subprocess.run([str(directory / 'cover')], check=True)
print('Dashboard cover selection, cache, and failure fallback work')
