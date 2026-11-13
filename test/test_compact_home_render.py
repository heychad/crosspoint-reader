"""Check production compact Home drawing, geometry, failures, and cache reuse."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
minimal = (root/'src/components/themes/minimal/MinimalHomeTheme.cpp').read_text()
dashboard = (root/'src/components/themes/minimal/DashboardHomeTheme.cpp').read_text()

def implementation(source):
    return '\n'.join(line for line in source.splitlines() if not line.startswith('#include'))

program = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "components/themes/minimal/CompactHomeLayout.h"
#include "components/themes/minimal/HomeDashboardData.h"
#include "components/themes/minimal/HomeDashboardDetails.h"
namespace EpdFontFamily {enum Style {REGULAR,BOLD};}
constexpr int UI_10_FONT_ID=10,UI_12_FONT_ID=12;
struct Scale{int bodyFontId=12;};Scale uiScaleSpec(){return {};}
struct Rect{int x,y,width,height;explicit Rect(int xx=0,int yy=0,int w=0,int h=0):x(xx),y(yy),width(w),height(h){}};
enum class Color {Black,White,LightGray};
enum class StrId {STR_STATS_PROGRESS,STR_STATS_TOTAL_TIME,STR_BAR_TIME_BOOK,STR_STATS_PAGES_PER_MINUTE,
 STR_STATS_DAILY_AVERAGE,STR_STATS_DAYS_READING,STR_STATS_FINISHED,STR_STATS_PROJECTED_FINISH,STR_STATS_SESSIONS,
 STR_STATS_AVERAGE_SESSION,STR_STATS_CURRENT_STREAK,STR_STATS_MORNING,STR_STATS_AFTERNOON,STR_STATS_EVENING,
 STR_STATS_NIGHT,STR_STATS_TIME_OF_DAY,STR_STATS_COMPLETED,STR_STATS_STARTED};
struct I18n{static I18n& getInstance(){static I18n i;return i;}const char*get(StrId id){
 static const char* labels[]={"Progress","Reading time","Time left","Pages per minute","Daily average","Days reading",
 "Finished","Estimated finish","Sessions","Average session","Current streak","Morning","Afternoon","Evening","Night","Time of day","Completed books","Started reading"};
 return labels[static_cast<int>(id)];}};
void formatReadingStatsShortDate(const ReadingStatsDate& date,char* out,size_t size){
 if(date.year)snprintf(out,size,"%04u-%02u-%02u",date.year,date.month,date.day);else snprintf(out,size,"-");}
struct BookReadingStats{static void formatDuration(unsigned value,char* out,size_t size){snprintf(out,size,"%us",value);}};
struct RecentBook{std::string title="A long book title that wraps across more than one line",author="Author name",coverBmpPath="cover.bmp";};
bool openFails=false,headerFails=false,drawFails=false;
int artDraws=0,placeholders=0;bool dirty=false;
struct HalFile{};
struct {bool openFileForRead(const char*,const std::string&,HalFile&){return !openFails;}}Storage;
enum class BmpReaderError{Ok,Failed};
struct Bitmap{Bitmap(HalFile&){}BmpReaderError parseHeaders(){return headerFails?BmpReaderError::Failed:BmpReaderError::Ok;}};
struct Text{Rect box;std::string value;};
struct GfxRenderer{
 int width=480,height=800,body=20,small=16;
 std::vector<Text>texts;
 int getScreenHeight()const{return height;}
 int getLineHeight(int font)const{return font==UI_10_FONT_ID?small:body;}
 int glyph(int font)const{return font==UI_10_FONT_ID?6:(body==32?12:8);}
 int getTextWidth(int font,const char* text,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{return int(strlen(text))*glyph(font);}
 void bounds(int x,int y,int w,int h)const{assert(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=width&&y+h<=height);}
 void fillRect(int x,int y,int w,int h,bool black=true){bounds(x,y,w,h);if(!black)dirty=false;}
 void fillRectDither(int x,int y,int w,int h,Color){bounds(x,y,w,h);}
 void maskRoundedRectOutsideCorners(int x,int y,int w,int h,int,Color){bounds(x,y,w,h);}
 void drawRoundedRect(int x,int y,int w,int h,int,int,bool){bounds(x,y,w,h);}
 std::string truncatedText(int font,const char* text,int w,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{return std::string(text).substr(0,std::max(0,w)/glyph(font));}
 std::vector<std::string>wrappedText(int font,const char* text,int w,int lines,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{
  std::vector<std::string>result;const std::string all=text;const size_t n=std::max(1,w/glyph(font));
  for(size_t at=0;at<all.size()&&int(result.size())<lines;at+=n)result.push_back(all.substr(at,n));return result;
 }
 void drawText(int font,int x,int y,const char* text,bool=true,EpdFontFamily::Style=EpdFontFamily::REGULAR){
  Rect box{x,y,getTextWidth(font,text),getLineHeight(font)};bounds(x,y,box.width,box.height);texts.push_back({box,text});
 }
 void noOverlap()const{
  for(size_t i=0;i<texts.size();++i)for(size_t j=i+1;j<texts.size();++j){
   const auto&a=texts[i].box;const auto&b=texts[j].box;if(a.width==0||b.width==0)continue;
   assert(a.x+a.width<=b.x||b.x+b.width<=a.x||a.y+a.height<=b.y||b.y+b.height<=a.y);
  }
 }
};
struct UITheme{
 struct Metrics{int homeTopPadding=56;};
 static UITheme&getInstance(){static UITheme t;return t;}
 Rect getContentArea(const GfxRenderer&r)const{return Rect{8,0,r.width-16,r.height};}
 Metrics getMetrics()const{return {};}
 static std::string getCoverThumbPath(const std::string& path,int height){assert(height>0);return path;}
};
struct MinimalHomeTheme{
 static Rect coverRect(const GfxRenderer&,Rect);
 int homeCoverThumbHeight(const GfxRenderer&)const;
 void drawRecentBookCover(GfxRenderer&,Rect,const std::vector<RecentBook>&,int,bool&,bool&,bool&,std::function<bool()>)const;
 static void drawReadingInfo(GfxRenderer&,Rect,const HomeDashboardData&);
 void drawEmptyRecents(GfxRenderer&,Rect)const{}
 void drawCoverPlaceholder(GfxRenderer&r,Rect box)const{assert(!dirty);r.bounds(box.x,box.y,box.width,box.height);++placeholders;}
 bool drawCoverThumbFill(GfxRenderer&r,Bitmap&,Rect box)const{r.bounds(box.x,box.y,box.width,box.height);++artDraws;dirty=drawFails;return !drawFails;}
};
struct DashboardHomeTheme:MinimalHomeTheme{
 static Rect coverRect(const GfxRenderer&,Rect);
 int homeCoverThumbHeight(const GfxRenderer&)const;
 void drawRecentBookCover(GfxRenderer&,Rect,const std::vector<RecentBook>&,int,bool&,bool&,bool&,std::function<bool()>)const;
 static void drawReadingInfo(GfxRenderer&,Rect,const RecentBook&,const HomeDashboardData&,const HomeDashboardDetails&);
};
''' + implementation(minimal) + '\n' + implementation(dashboard) + r'''
int main(){
 for(int width:{480,800})for(int body:{20,32})for(bool clock:{false,true})for(bool known:{false,true})for(bool global:{false,true})for(int failure:{0,1,2,3}){
  GfxRenderer r;r.width=width;r.height=width==480?800:480;r.body=body;r.small=body==32?20:16;
  const Rect area{8,56,width-16,compacthomelayout::tileHeight(r.height,56)};
  openFails=failure==1;headerFails=failure==2;drawFails=failure==3;
  std::vector<RecentBook>books(1);HomeDashboardData basic;basic.progress=42;basic.hasStats=known;basic.readingSeconds=3600;basic.remainingSeconds=1800;
  HomeDashboardDetails detail;detail.hasClock=clock;detail.hasPace=known;detail.pagesPerMinuteTenths=25;detail.hasDays=known;
  detail.sessions=3;detail.averageSessionSeconds=1200;detail.dailyAverageSeconds=900;detail.daysReading=4;
  detail.startDate={2026,11,10};detail.finishDate={2026,11,15};detail.hasGlobal=global;
  detail.globalSeconds=7200;detail.completedBooks=5;detail.hasReaderType=known;detail.readerType=ReadingTimeBucket::Evening;
  strcpy(detail.chapter,"Chapter title replaces the author when a saved title is available");
  for(bool dashboard:{false,true}){
   r.texts.clear();bool rendered=false,stored=false,restored=false;int saves=0;auto save=[&]{++saves;return true;};
   if(dashboard){
    DashboardHomeTheme theme;theme.drawRecentBookCover(r,area,books,0,rendered,stored,restored,save);
    assert(theme.homeCoverThumbHeight(r)==theme.coverRect(r,area).height);
    theme.drawReadingInfo(r,area,books.front(),basic,detail);r.noOverlap();
    const auto has=[&](const char* value){return std::any_of(r.texts.begin(),r.texts.end(),[&](const Text&t){return t.value==value;});};
    assert(has("42%"));
    if(known){assert(has("3600s")&&has("1800s")&&has("2.5"));
     if(clock)assert(has("2026-11-10")&&has("2026-11-15")&&has("900s"));
     else assert(has("3")&&has("1200s"));
    }else assert(!has("3600s")&&!has("1800s"));
    if(global&&clock)assert(has("Current streak")&&has("Time of day"));
    if(global&&!clock)assert(has("7200s")&&has("Completed books"));
    if(!global)assert(!has("Current streak")&&!has("Completed books"));
    restored=true;int old=artDraws;theme.drawRecentBookCover(r,area,books,1,rendered,stored,restored,save);assert(old==artDraws&&saves==1);
    restored=false;theme.drawRecentBookCover(r,area,books,1,rendered,stored,restored,[]{return false;});assert(!rendered&&!stored);
   }else{
    MinimalHomeTheme theme;theme.drawRecentBookCover(r,area,books,0,rendered,stored,restored,save);
    assert(theme.homeCoverThumbHeight(r)==theme.coverRect(r,area).height);
    theme.drawReadingInfo(r,area,basic);r.noOverlap();
    restored=true;int old=artDraws;theme.drawRecentBookCover(r,area,books,1,rendered,stored,restored,save);assert(old==artDraws&&saves==1);
   }
  }
 }
 assert(placeholders>0);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p=Path(tmp);(p/'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-fsanitize=address,undefined','-I',str(root/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Compact Home drawing fits both sizes/orientations; live fields, failures, and cached art pass')
