"""Exercise the production sleep dashboard's layout with a simulated display."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/boot_sleep/ReadingStatsSleep.cpp').read_text()
source = source[source.index('namespace sleepstats {'):]
ids = sorted(set(re.findall(r'\bSTR_[A-Z_]+\b', source)))
prelude = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
enum class StrId { IDS };
using enum StrId;
const char* tr(StrId id) { static std::string s; s=std::to_string(static_cast<int>(id)); return s.c_str(); }
struct { const char* get(StrId id) { return tr(id); } } I18N;
struct EpdFontFamily { enum Style { REGULAR, BOLD }; };
constexpr int SMALL_FONT_ID=1, UI_10_FONT_ID=2, UI_12_FONT_ID=3;
struct Rect { int x,y,width,height; };
struct GfxRenderer {
 int width=480,height=800,fontScale=1; mutable bool cleared=false; mutable std::vector<std::string> text;
 int getLineHeight(int f) const { return (f==SMALL_FONT_ID?10:14)*fontScale; }
 int getTextWidth(int,const char* s,EpdFontFamily::Style=EpdFontFamily::REGULAR) const {return int(strlen(s))*5;}
 void valid(int x,int y,int w,int h) const { assert(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=width && y+h<=height); }
 void drawText(int f,int x,int y,const char* s,bool,EpdFontFamily::Style st) const {valid(x,y,getTextWidth(f,s,st),getLineHeight(f)); text.emplace_back(s);}
 void drawRect(int x,int y,int w,int h) const {valid(x,y,w,h);}
 void drawLine(int x,int y,int x2,int y2) const {assert(x2>=x && y2>=y);valid(x,y,x2-x+1,y2-y+1);}
 void fillRect(int x,int y,int w,int h) const {valid(x,y,w,h);}
 void clearScreen() const {cleared=true;}
};
struct UITheme {static Rect getContentArea(const GfxRenderer& r){return {0,0,r.width,r.height};}};
struct Bitmap {}; bool coverDrawOk=true;
struct BaseTheme {static bool drawCoverThumbFill(const GfxRenderer& r,const Bitmap&,Rect box){r.valid(box.x,box.y,box.width,box.height);return coverDrawOk;}};
struct ReadingStatsDate {uint16_t year=0;uint8_t month=0,day=0;bool isValid()const{return year&&month&&day;}};
struct ReadingStatsDateTime {};
struct BookReadingStats {
 uint16_t sessionCount=0; uint32_t totalReadingSeconds=0,totalPagesTurned=0; bool isCompleted=false;
 ReadingStatsDate startDate,finishedDate; std::array<uint32_t,4> timeOfDaySeconds{};std::array<uint32_t,7> dayOfWeekSeconds{};
 static void formatDuration(uint32_t n,char* out,size_t size){snprintf(out,size,"%us",n);}
 uint32_t remainingReadingSeconds(int) const {return 0;}
 ReadingStatsDate projectedFinishDate(const ReadingStatsDateTime&,int) const {return {};}
};
struct GlobalReadingStats { uint32_t totalSessions=0,totalReadingSeconds=0,totalPagesTurned=0; };
struct GlobalStatsAggregate {GlobalReadingStats stats;uint32_t sources=0,skipped=0;bool complete=false;};
namespace sleepstats {
struct Data { BookReadingStats book;GlobalReadingStats device;GlobalStatsAggregate allDevices;ReadingStatsDateTime now;int progress=-1;bool hasClock=false,hasDevice=false,hasAllDevices=false;};
struct DashboardData {int progress=-1;uint32_t readingSeconds=0,remainingSeconds=0,sessions=0,pages=0,globalSeconds=0,completedBooks=0,dailyAverageSeconds=0;ReadingStatsDate startDate;uint16_t currentStreak=0;bool hasStats=false,hasGlobal=false,hasDailyAverage=false,hasStreak=false;};
}
int utf8SafeTruncateBuffer(const char* b,int n){if(!n)return 0;int p=n-1;while(p>0 && (static_cast<unsigned char>(b[p])&0xc0)==0x80)--p;unsigned char c=b[p];int len=c<128?1:c<224?2:c<240?3:4;return n-p<len?p:n;}
namespace readingcharts {int barWidth(uint32_t v,uint32_t max,int width){return max?int(uint64_t(v)*width/max):0;}}
'''.replace('IDS', ','.join(ids))
main = r'''
bool saw(const GfxRenderer& r,const char* s){return std::find(r.text.begin(),r.text.end(),s)!=r.text.end();}
int main(){
 sleepstats::Data data; data.book.sessionCount=4;data.book.totalReadingSeconds=400;data.book.timeOfDaySeconds[0]=20;data.book.dayOfWeekSeconds[6]=30;
 GfxRenderer r;data.hasClock=true;data.progress=37;
 assert(sleepstats::draw(r,"Sample title",data));assert(r.cleared && saw(r,"37%") && saw(r,"Sample title"));
 GfxRenderer unknown;data.hasClock=false;data.progress=-1;data.hasDevice=false;
 assert(sleepstats::draw(unknown,"Book",data));assert(saw(unknown,"-"));
 GfxRenderer device;data.hasDevice=true;
 assert(sleepstats::draw(device,"Book",data));assert(saw(device,tr(STR_STATS_DEVICE)));
 data.hasAllDevices=true;data.allDevices.sources=2;data.allDevices.stats.totalReadingSeconds=1234;data.allDevices.complete=true;
 GfxRenderer all;assert(sleepstats::draw(all,"Book",data));assert(saw(all,tr(STR_STATS_ALL_DEVICES)) && saw(all,"1234s"));
 data.allDevices.complete=false;GfxRenderer partial;assert(sleepstats::draw(partial,"Book",data));assert(saw(partial,tr(STR_STATS_PARTIAL)));
 for(int h=120;h<=820;h+=7){GfxRenderer r;r.height=h;sleepstats::draw(r,"Book",data);}
 GfxRenderer landscape;landscape.width=800;landscape.height=480;assert(sleepstats::draw(landscape,"Book",data));
 for(int scale : {1,2})for(int w : {480,800}){
  GfxRenderer sized;sized.width=w;sized.height=w==480?800:480;sized.fontScale=scale;
  sleepstats::draw(sized,"Book",data);
 }
 data.hasAllDevices=false;
 GfxRenderer small;small.width=200;small.height=200;assert(!sleepstats::draw(small,"Book",data));assert(!small.cleared);
 GfxRenderer summary;data.progress=37;assert(sleepstats::drawSummary(summary,"Sample title",data));assert(summary.cleared && saw(summary,"37%") && saw(summary,"Sample title"));
 GfxRenderer emptySummary;data.progress=-1;assert(sleepstats::drawSummary(emptySummary,"Book",data));assert(saw(emptySummary,"-"));
 GfxRenderer tinySummary;tinySummary.width=200;tinySummary.height=200;assert(!sleepstats::drawSummary(tinySummary,"Book",data));assert(!tinySummary.cleared);
 for(int height=120;height<=820;height+=7){GfxRenderer resized;resized.height=height;sleepstats::drawSummary(resized,"Book",data);}
 Bitmap cover;GfxRenderer withCover;data.progress=42;assert(sleepstats::drawCoverSummary(withCover,"Book",data,cover));assert(saw(withCover,"42%") && saw(withCover,"Book"));
 GfxRenderer badCover;coverDrawOk=false;assert(!sleepstats::drawCoverSummary(badCover,"Book",data,cover));coverDrawOk=true;
 for(int height=120;height<=820;height+=7){GfxRenderer resized;resized.height=height;sleepstats::drawCoverSummary(resized,"Book",data,cover);}
 GfxRenderer minimal;assert(sleepstats::drawMinimalBook(minimal,"Book",37,400,&cover));
 assert(minimal.cleared && saw(minimal,"37%") && saw(minimal,"400s"));
 GfxRenderer placeholder;assert(sleepstats::drawMinimalBook(placeholder,"Unknown book",-1,0,nullptr));
 assert(placeholder.cleared && saw(placeholder,"Unknown book") && !saw(placeholder,"0%"));
 GfxRenderer clipped;assert(sleepstats::drawMinimalBook(clipped,"Book",140,0,&cover) && saw(clipped,"100%"));
 GfxRenderer brokenCover;coverDrawOk=false;
 assert(sleepstats::drawMinimalBook(brokenCover,"Fallback",42,0,&cover) && saw(brokenCover,"Fallback"));
 coverDrawOk=true;
 for(int height=120;height<=820;height+=7){GfxRenderer resized;resized.height=height;sleepstats::drawMinimalBook(resized,"Book",37,400,&cover);}
 sleepstats::DashboardData dashboard;dashboard.progress=42;dashboard.hasStats=true;dashboard.readingSeconds=600;dashboard.pages=30;dashboard.sessions=3;dashboard.hasGlobal=true;dashboard.globalSeconds=1200;dashboard.completedBooks=7;dashboard.startDate={2026,11,8};dashboard.hasDailyAverage=true;dashboard.dailyAverageSeconds=300;dashboard.hasStreak=true;dashboard.currentStreak=5;
 GfxRenderer dashboardScreen;assert(sleepstats::drawBookDashboard(dashboardScreen,"Book","Author",dashboard,&cover));
 assert(saw(dashboardScreen,"42%") && saw(dashboardScreen,"Book") && saw(dashboardScreen,"Author") && saw(dashboardScreen,"7") && saw(dashboardScreen,"2026-11-08") && saw(dashboardScreen,"300s") && saw(dashboardScreen,"5"));
 for(int height=120;height<=820;height+=7){GfxRenderer resized;resized.height=height;sleepstats::drawBookDashboard(resized,"Book","Author",dashboard,&cover);}
 GfxRenderer dashboardFallback;dashboard.hasStats=false;dashboard.hasGlobal=false;
 assert(sleepstats::drawBookDashboard(dashboardFallback,"No cover","",dashboard,nullptr));assert(saw(dashboardFallback,"No cover"));
 for(int height=120;height<=820;height+=7){GfxRenderer resized;resized.height=height;sleepstats::drawBookDashboard(resized,"Book","Author",dashboard,&cover);}
 GfxRenderer wide;wide.width=800;wide.height=480;data.hasClock=true;
 assert(sleepstats::draw(wide,"An extremely long book title that must be clipped to fit even when translated",data));
}
'''
with tempfile.TemporaryDirectory() as d:
    source_file = Path(d) / 'render.cpp'
    source_file.write_text(prelude + source + main)
    executable = Path(d) / 'render'
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(source_file), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Statistics sleep rendering and fallback geometry passed')
