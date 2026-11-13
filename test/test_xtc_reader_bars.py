"""Exercise production XTC bar overlays, bitmap-strip clearing, and titles."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'src/activities/reader/XtcReaderActivity.cpp').read_text()
def method(signature):
 start=source.index(signature);opening=source.index('{',start);depth=0
 for end in range(opening,len(source)):
  depth+=(source[end]=='{')-(source[end]=='}')
  if depth==0:return source[start:end+1]
 raise ValueError(signature)
code=r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "personalization/ReaderBars.h"
#define tr(x) "Unnamed"
struct CrossPointSettings {enum {XTC_STATUS_BAR_HIDE=0,XTC_STATUS_BAR_BOTTOM=1,XTC_STATUS_BAR_TOP=2,XTC_STATUS_BAR_BOTH=3};enum{BOOK_TITLE=0,CHAPTER_TITLE=1};};
struct Settings {bool readerBarsEnabled=true,legacyXtcTopUsesBottom=false,tapToHideStatusBar=true;ReaderBar readerTopBar,readerBottomBar;int mode=3;
 struct Spec{int xtcMode,titleMode;};Spec statusBarSpec()const{return{mode,CrossPointSettings::CHAPTER_TITLE};}}SETTINGS;
struct GfxRenderer {int height=120,width=80,mt=9,mb=3,mr=7,ml=7;std::vector<int> cleared;int legacy=0,custom=0;
 int getScreenHeight()const{return height;}int getScreenWidth()const{return width;}
 void getOrientedViewableTRBL(int*t,int*r,int*b,int*l)const{*t=mt;*r=mr;*b=mb;*l=ml;}
 void fillRect(int x,int y,int w,int h,bool black){assert(x==0 && y>=0 && y+h<=height && w==width && h>0 && !black);cleared.push_back(y);}
};
struct UITheme{static UITheme& getInstance(){static UITheme instance;return instance;}int getStatusBarHeight()const{return 15;}};
struct Gui{void drawStatusBar(GfxRenderer&r,float,int,int,const std::string&,int){++r.legacy;}}GUI;
namespace xtc{struct ChapterInfo{std::string name;uint16_t startPage,endPage;};}
struct Xtc{std::vector<xtc::ChapterInfo> chapters{{"Chapter One",1,4}};bool hasChapters()const{return !chapters.empty();}const auto& getChapters()const{return chapters;}int getPageCount()const{return 10;}std::string getTitle()const{return "Book Title";}};
struct BookReadingStats{static void formatDuration(uint32_t s,char*out,size_t n){std::snprintf(out,n,"%us",s);}};
size_t utf8SafeTruncateBuffer(const char*,int n){return n;}
struct XtcReaderActivity {enum class StatusBarOverlayPosition{Bottom,Top};struct StatusBarInfo{int currentPage,pageCount;std::string title;};
 std::unique_ptr<Xtc>xtc=std::make_unique<Xtc>();uint32_t currentPage=2;std::atomic<bool>statusBarsVisible{true};
 struct Stats{mutable int bookCalls=0,chapterCalls=0;uint32_t bookTimeLeft(int)const{++bookCalls;return 100;}uint32_t sectionTimeLeft(int,int)const{++chapterCalls;return 10;}}readingStats;
 int getProgressBasisPoints()const{return 3000;}
 StatusBarInfo getStatusBarInfo(bool,bool)const;
 void renderStatusBarOverlay(GfxRenderer&,StatusBarOverlayPosition,bool=true)const;
};
struct Draw{ReaderBar config;ReaderBarPosition position;std::string book,chapter,time;};std::vector<Draw>draws;
namespace readerbars {
 int height(const GfxRenderer&,const ReaderBar& b){return b.height(12,1);}
 void draw(GfxRenderer&r,const ReaderBar& b,ReaderBarPosition p,float progress,int page,int pages,bool,
           const char* book,const char*chapter,bool,const char*,const char*bookTime,const char*sectionTime,const char*){
  assert(progress==30 && page==2 && pages==4);++r.custom;draws.push_back({b,p,book,chapter,sectionTime});
  if(b.contains(ReaderBarItem::TimeLeftBook))assert(std::string(bookTime)=="~100s");
 }
}
// INSERT_METHODS
int main(){
 for(int mode=0;mode<4;++mode)for(bool custom:{false,true})for(bool compatibility:{false,true})
 for(bool hideTop:{false,true})for(bool hideBottom:{false,true})for(bool visible:{false,true})for(bool content:{false,true}){
  SETTINGS.mode=mode;SETTINGS.readerBarsEnabled=custom;SETTINGS.legacyXtcTopUsesBottom=compatibility;
  SETTINGS.readerTopBar={};SETTINGS.readerBottomBar={};
  SETTINGS.readerTopBar.slots[0]=ReaderBarItem::BookTitle;SETTINGS.readerTopBar.slots[1]=ReaderBarItem::TimeLeftBook;
  SETTINGS.readerBottomBar.slots[0]=ReaderBarItem::ChapterTitle;SETTINGS.readerBottomBar.slots[1]=ReaderBarItem::TimeLeftSection;
  SETTINGS.readerTopBar.hidden=hideTop;SETTINGS.readerBottomBar.hidden=hideBottom;
  XtcReaderActivity reader;reader.statusBarsVisible=visible;GfxRenderer display;draws.clear();
  reader.renderStatusBarOverlay(display,XtcReaderActivity::StatusBarOverlayPosition::Top,content);
  reader.renderStatusBarOverlay(display,XtcReaderActivity::StatusBarOverlayPosition::Bottom,content);
  const int strips=mode==0?0:mode==3?2:1;assert(static_cast<int>(display.cleared.size())==strips);
  const int top=(mode==2 || mode==3) && (!custom || !hideTop);
  const int bottom=(mode==1 || mode==3) && (!custom || !hideBottom);
  const int expected=visible && content?top+bottom:0;
  assert(display.custom==(custom?expected:0) && display.legacy==(custom?0:expected));
  for(const auto& call:draws){
   if(call.position==ReaderBarPosition::Top && !compatibility)assert(call.book=="Book Title" && call.chapter.empty());
   else assert(call.chapter=="Chapter One" && call.time=="~10s");
  }
  assert(reader.currentPage==2);
  if(!content || !visible)assert(reader.readingStats.bookCalls==0 && reader.readingStats.chapterCalls==0);
 }
 XtcReaderActivity reader;auto info=reader.getStatusBarInfo(true,false);assert(info.title=="Chapter One" && info.currentPage==2 && info.pageCount==4);
 reader.xtc->chapters.clear();info=reader.getStatusBarInfo(true,false);assert(info.title.empty() && info.currentPage==3 && info.pageCount==10);
 info=reader.getStatusBarInfo(false,true);assert(info.title=="Book Title");
}
'''
code=code.replace('// INSERT_METHODS',method('XtcReaderActivity::StatusBarInfo XtcReaderActivity::getStatusBarInfo(')+'\n'+method('void XtcReaderActivity::renderStatusBarOverlay('))
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',*shlex.split(os.environ.get('CXXFLAGS','')),'-I',str(ROOT/'src'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('XTC placement, custom/legacy layouts, hidden strips, render masks, titles, and time estimates passed')
