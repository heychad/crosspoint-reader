"""Exercise Carousel's saved reading time and progress footer."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/components/themes/lyra/LyraCarouselTheme.cpp').read_text()
start = source.index('void LyraCarouselTheme::drawReadingProgress(')
end = source.index('\nvoid LyraCarouselTheme::drawRecentBookCover(', start)
program = r'''
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "components/themes/lyra/CarouselLayout.h"
#include "components/themes/minimal/HomeDashboardData.h"
constexpr int SMALL_FONT_ID=1;
enum class Color {LightGray};
struct Rect {int x,y,width,height;};
struct BookReadingStats {static void formatDuration(unsigned s,char* b,size_t n){snprintf(b,n,"%us",s);}};
struct GfxRenderer {
 std::vector<int> fills; std::vector<std::string> texts; int tracks=0;
 int getLineHeight(int)const{return 16;}
 int getTextWidth(int,const char* p)const{return int(std::string(p).size())*5;}
 void fillRectDither(int,int,int,int,Color){++tracks;}
 void fillRect(int,int,int width,int,bool){fills.push_back(width);}
 void drawText(int,int,int,const char* text,bool){texts.emplace_back(text);}
};
class LyraCarouselTheme {public:static void drawReadingProgress(GfxRenderer&,Rect,const HomeDashboardData&);};
''' + source[start:end] + r'''
int main(){
 GfxRenderer r; Rect area{10,56,460,480}; HomeDashboardData d;
 LyraCarouselTheme::drawReadingProgress(r,area,d);assert(r.texts.empty() && !r.tracks);
 d.hasStats=true;d.readingSeconds=120;LyraCarouselTheme::drawReadingProgress(r,area,d);
 assert(r.texts.size()==1 && r.texts[0]=="120s" && !r.tracks);
 r={};d.progress=42;LyraCarouselTheme::drawReadingProgress(r,area,d);
 assert(r.texts.size()==2 && r.texts[0]=="120s" && r.texts[1]=="42%" && r.tracks==1 && r.fills[0]==93);
 r={};d.hasStats=false;d.progress=150;LyraCarouselTheme::drawReadingProgress(r,area,d);
 assert(r.texts.size()==1 && r.texts[0]=="100%" && r.fills[0]==222);
 r={};d.progress=0;LyraCarouselTheme::drawReadingProgress(r,area,d);
 assert(r.texts.size()==1 && r.texts[0]=="0%" && r.tracks==1 && r.fills.empty());
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / 'test.cpp'; binary = Path(tmp) / 'test'
    cpp.write_text(program)
    subprocess.run(['c++', '-std=c++17', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Carousel reading footer displays available statistics')
