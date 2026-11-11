"""Run production Carousel drawing against display-bound checks."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'src/components/themes/lyra/LyraCarouselTheme.cpp').read_text()
def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
program = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "components/themes/lyra/CarouselLayout.h"
constexpr int SMALL_FONT_ID=1;
const unsigned char iconData=0;
const auto FolderIcon=&iconData, LibraryIcon=&iconData, BookmarkIcon=&iconData, TransferIcon=&iconData,
           Settings2Icon=&iconData, BlocksIcon=&iconData, CoverIcon=&iconData;
enum class UIIcon {Folder, Library, Bookmark, Transfer, Settings, Plugins, None};
struct Rect {int x,y,width,height;};
struct RecentBook {std::string title="A book",coverBmpPath;};
struct GfxRenderer {
 int width=480,height=800,line=16;
 int getLineHeight(int) const {return line;}
 int getTextWidth(int,const char* text)const{return std::string(text).size()*6;}
 void bounds(int x,int y,int w,int h)const{assert(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=width&&y+h<=height);}
 void drawIcon(const unsigned char*,int x,int y,int size){bounds(x,y,size,size);}
 void drawRect(int x,int y,int w,int h,bool){bounds(x,y,w,h);}
 void fillRect(int x,int y,int w,int h,bool){bounds(x,y,w,h);}
 void drawRoundedRect(int x,int y,int w,int h,int,int,bool){bounds(x,y,w,h);}
 void drawText(int f,int x,int y,const char* text,bool){bounds(x,y,getTextWidth(f,text),line);}
 std::string truncatedText(int,const char* text,int w){return std::string(text).substr(0,w/6);}
 std::vector<std::string> wrappedText(int,const char*,int width,int){return {std::string(std::min(30,width/6),'X'),"Second line"};}
};
struct HalFile{bool close(){return true;}};
struct {bool openFileForRead(const char*,const std::string&,HalFile&){return false;}} Storage;
enum class BmpReaderError{Ok};
struct Bitmap{Bitmap(HalFile&){} BmpReaderError parseHeaders(){return BmpReaderError::Ok;}};
struct UITheme{static std::string getCoverThumbPath(const std::string& path,int){return path;}};
class LyraCarouselTheme {
 mutable int centerIndex=0,cachedIndex=-1;
public:
 static void drawHomeMenu(GfxRenderer&,Rect,int,int,const std::function<std::string(int)>&,const std::function<UIIcon(int)>&);
 void drawRecentBookCover(GfxRenderer&,Rect,const std::vector<RecentBook>&,int,bool&,bool&,bool&,std::function<bool()>)const;
 void drawEmptyRecents(GfxRenderer&,Rect)const{}
 int homeCoverThumbHeight(const GfxRenderer&)const{return 400;}
 bool drawCoverThumbFill(GfxRenderer&,Bitmap&,Rect)const{return true;}
};
''' + method('void LyraCarouselTheme::drawHomeMenu(') + '\n' + method('void LyraCarouselTheme::drawRecentBookCover(') + r'''
int main(){
 for(int width:{480,800})for(int line:{16,32})for(int count:{0,1,2,3}){
  GfxRenderer r;r.width=width;r.height=width==480?800:480;r.line=line;
  const int mh=carousellayout::menuHeight(line),my=r.height-40-mh;
  const Rect area{8,56,width-16,carousellayout::coverHeight(480,r.height,56,40,line)};
  std::vector<RecentBook> books(count);LyraCarouselTheme theme;
  for(int selected=0;selected<count+6;++selected){
   bool rendered=false,stored=false,restored=false;
   theme.drawRecentBookCover(r,area,books,selected,rendered,stored,restored,[]{return true;});
   LyraCarouselTheme::drawHomeMenu(r,{8,my,width-16,mh},6,selected-count,
      [](int){return std::string("Settings and preferences");},[](int){return UIIcon::Settings;});
  }
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++','-std=c++20','-I',str(root / 'src'),str(path / 'test.cpp'),'-o',str(path / 'test')],check=True)
    subprocess.run([str(path / 'test')],check=True)
print('Carousel drawing stays within portrait and landscape bounds')
