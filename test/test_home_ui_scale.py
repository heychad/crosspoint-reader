"""Test real Lyra home menu drawing at both interface sizes."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/components/themes/lyra/LyraTheme.cpp').read_text()
def method(signature):
    start=source.index(signature); opening=source.index('{',start); end=opening+1; depth=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
title_start=source.index('    const int bodyFont = uiScaleSpec().bodyFontId;', source.index('void LyraTheme::drawRecentBookCover('))
title_end=source.index('\n  } else {', title_start)
title_block=source[title_start:title_end]
code=r'''
#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "components/UIScale.h"
struct Rect {int x,y,width,height;bool operator==(const Rect&)const=default;};
enum class Color {LightGray};enum class UIIcon {None, Known};
struct EpdFontFamily {enum Style{REGULAR,BOLD};};
constexpr int STR_NO_OPEN_BOOK=0, STR_START_READING=1;
const char* tr(int id){return id==0?"No book":"Read";}
namespace LyraMetrics {struct Metrics {int contentSidePadding=20,menuRowHeight=50,menuSpacing=8,verticalSpacing=8;};constexpr Metrics values{};}
constexpr int cornerRadius=6,mainMenuIconSize=24,hPaddingInSelection=8;
const uint8_t* iconForName(UIIcon icon){static const uint8_t pixels=0;return icon==UIIcon::Known?&pixels:nullptr;}
struct Draw {int font,x,y;std::string text;};
struct GfxRenderer {
 mutable std::vector<Draw> text; mutable std::vector<Rect> selections; mutable std::vector<int> iconY;
 int getLineHeight(int font)const{return font==UI_10_FONT_ID?10:12;}
 void drawText(int font,int x,int y,const char* s,bool,EpdFontFamily::Style=EpdFontFamily::REGULAR)const{text.push_back({font,x,y,s});}
 void fillRoundedRect(int x,int y,int w,int h,int,Color)const{selections.push_back({x,y,w,h});}
 void drawIcon(const uint8_t*,int,int y,int)const{iconY.push_back(y);}
 std::vector<std::string> wrappedText(int font,const char* s,int,int,EpdFontFamily::Style)const{
  assert(font==uiScaleSpec().bodyFontId);if(s[0]=='L')return {"Long","book","title"};return {s};}
 std::string truncatedText(int,const char* s,int)const{return s;}
};
struct LyraTheme {
 void drawButtonMenu(GfxRenderer&,Rect,int,int,const std::function<std::string(int)>&,const std::function<UIIcon(int)>&)const;
 void drawEmptyRecents(const GfxRenderer&,Rect)const;
};
''' + method('void LyraTheme::drawButtonMenu(') + '\n' + method('void LyraTheme::drawEmptyRecents(') + r'''
struct Book {std::string title="Title",author="Author";};
void drawTitle(const GfxRenderer& renderer,const Book& book){int textWidth=180,tileY=20,tileHeight=100,tileX=0,coverWidth=40;
''' + title_block + r'''
}
int main(){LyraTheme theme;Rect area{0,20,400,200};
 auto labels=[](int i){return i==0?"Library":"Settings";};std::function<UIIcon(int)> noIcons;
 uiscale::publish(1);GfxRenderer large;theme.drawButtonMenu(large,area,2,1,labels,noIcons);theme.drawEmptyRecents(large,area);
 uiscale::publish(0);GfxRenderer small;theme.drawButtonMenu(small,area,2,1,labels,noIcons);theme.drawEmptyRecents(small,area);
 assert(large.selections==small.selections && large.selections.size()==1);
 assert(large.text.size()==4 && small.text.size()==4);
 for(int i=0;i<3;++i){assert(large.text[i].font==UI_12_FONT_ID && small.text[i].font==UI_10_FONT_ID);}
 assert(large.text[3].font==UI_10_FONT_ID && small.text[3].font==UI_10_FONT_ID);
 assert(large.text[0].y==39 && small.text[0].y==40);
 assert(large.text[0].text==small.text[0].text && large.text[1].text==small.text[1].text);
 auto icons=[](int){return UIIcon::Known;};
 uiscale::publish(1);GfxRenderer largeIcons;theme.drawButtonMenu(largeIcons,area,2,1,labels,icons);
 uiscale::publish(0);GfxRenderer smallIcons;theme.drawButtonMenu(smallIcons,area,2,1,labels,icons);
 assert(largeIcons.iconY.size()==2 && largeIcons.iconY==smallIcons.iconY);
 uiscale::publish(1);GfxRenderer largeTitle;drawTitle(largeTitle,{});
 uiscale::publish(0);GfxRenderer smallTitle;drawTitle(smallTitle,{});
 assert(largeTitle.text.size()==2 && smallTitle.text.size()==2);
 assert(largeTitle.text[0].font==UI_12_FONT_ID && smallTitle.text[0].font==UI_10_FONT_ID);
 assert(largeTitle.text[1].font==UI_10_FONT_ID && smallTitle.text[1].font==UI_10_FONT_ID);
 Book longBook;longBook.title="Long title";
 uiscale::publish(1);GfxRenderer largeWrapped;drawTitle(largeWrapped,longBook);
 uiscale::publish(0);GfxRenderer smallWrapped;drawTitle(smallWrapped,longBook);
 assert(largeWrapped.text.size()==4 && smallWrapped.text.size()==4);
 assert(largeWrapped.text[1].y-largeWrapped.text[0].y==12 && smallWrapped.text[1].y-smallWrapped.text[0].y==10);
 assert(largeWrapped.text.front().y>=20 && largeWrapped.text.back().y<120);
 assert(smallWrapped.text.front().y>=20 && smallWrapped.text.back().y<120);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory);(path/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20','-I',str(root/'src'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Lyra home text follows interface scale while selection geometry stays fixed')
