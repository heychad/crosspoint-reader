"""Exercise Classic's real current-book text layout at both interface sizes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/components/themes/BaseTheme.cpp').read_text()
start = source.index('  const int bodyFont = uiScaleSpec().bodyFontId;', source.index('void BaseTheme::drawRecentBookCover'))
end = source.index('\n}\n\nint BaseTheme::getMenuRowHeight', start)
code = r'''
#include <cassert>
#include <string>
#include <vector>
#include "components/UIScale.h"
struct Rect {int x,y,width,height;};
struct Book {std::string title,author;};
struct Draw {int font,y; std::string text;};
constexpr int STR_NO_OPEN_BOOK=0, STR_START_READING=1, STR_CONTINUE_READING=2;
const char* tr(int id){return id==0?"No book":id==1?"Start":"Continue";}
struct GfxRenderer {
 std::vector<Draw> draws;
 int getLineHeight(int font)const{return font==UI_10_FONT_ID?10:12;}
 std::vector<std::string> wrappedText(int font,const char* title,int width,int max)const{
  assert(font==uiScaleSpec().bodyFontId && width>0 && max==3);
  return title[0]=='L'?std::vector<std::string>{"Long","book","title"}:std::vector<std::string>{title};}
 std::string truncatedText(int,const char* s,int)const{return s;}
 int getTextWidth(int,const char* s)const{return static_cast<int>(std::string(s).size())*5;}
 void fillRect(int,int,int,int,bool)const{}
 void drawRect(int,int,int,int,bool)const{}
 void drawCenteredText(int font,int y,const char* text,bool=true){draws.push_back({font,y,text});}
};
void render(GfxRenderer& renderer,bool hasContinueReading,bool coverRendered){
 std::vector<Book> recentBooks{{"Long book title","Author"}};
 bool bookSelected=true;int bookWidth=200,bookY=20,bookHeight=300;Rect rect{0,20,400,300};
''' + source[start:end] + r'''
}
int main(){
 for(bool cover : {false,true}) {
  uiscale::publish(1);GfxRenderer large;render(large,true,cover);
  uiscale::publish(0);GfxRenderer small;render(small,true,cover);
  assert(large.draws.size()==5 && small.draws.size()==5);
  for(int i=0;i<3;++i){
   assert(large.draws[i].font==UI_12_FONT_ID && small.draws[i].font==UI_10_FONT_ID);
   assert(large.draws[i].text==small.draws[i].text);
  }
  assert(large.draws[1].y-large.draws[0].y==12 && small.draws[1].y-small.draws[0].y==10);
  assert(large.draws[3].font==UI_10_FONT_ID && small.draws[3].font==UI_10_FONT_ID);
  assert(large.draws[4].font==UI_10_FONT_ID && small.draws[4].font==UI_10_FONT_ID);
  assert(large.draws[0].y>=20 && large.draws[4].y<320);
  assert(small.draws[0].y>=20 && small.draws[4].y<320);
 }
 uiscale::publish(1);GfxRenderer largeEmpty;render(largeEmpty,false,false);
 uiscale::publish(0);GfxRenderer smallEmpty;render(smallEmpty,false,false);
 assert(largeEmpty.draws.size()==2 && smallEmpty.draws.size()==2);
 assert(largeEmpty.draws[0].font==UI_12_FONT_ID && smallEmpty.draws[0].font==UI_10_FONT_ID);
 assert(largeEmpty.draws[1].font==UI_10_FONT_ID && smallEmpty.draws[1].font==UI_10_FONT_ID);
}
'''
with tempfile.TemporaryDirectory() as directory:
    target = Path(directory)
    (target / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(target / 'test.cpp'), '-o', str(target / 'test')], check=True)
    subprocess.run([str(target / 'test')], check=True)
print('Classic home text uses the selected interface scale')
