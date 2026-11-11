"""Exercise bounded page extraction and the shared live preview cache."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
section=(root/'lib/Epub/Epub/Section.cpp').read_text()
a=section.index('std::string Section::getTextFromSectionFile(')
b=section.index('std::optional<uint16_t> Section::getCachedPageCount()',a)
extract=r'''
#include <cassert>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>
enum{TAG_PageLine,TAG_PageImage};
struct TextBlock{std::vector<std::string>words;std::vector<bool>starts;
 unsigned wordCount()const{return words.size();}const char*wordText(int i)const{return words[i].c_str();}
 unsigned wordTextLen(int i)const{return words[i].size();}bool wordStartsParagraph(int i)const{return starts[i];}};
struct PageElement{virtual~PageElement()=default;virtual int getTag()const{return TAG_PageImage;}};
struct PageLine:PageElement{TextBlock block;int getTag()const override{return TAG_PageLine;}const TextBlock*getBlock()const{return &block;}};
struct Page{std::vector<std::unique_ptr<PageElement>>elements;};
struct Section{int currentPage=0;std::unique_ptr<Page> page=std::make_unique<Page>();
 std::unique_ptr<Page>loadPage(int){return std::move(page);}
 std::string getTextFromSectionFile(size_t maxBytes=std::numeric_limits<size_t>::max(),bool preserveParagraphs=false);};
Section fixture(){Section s;auto line=std::make_unique<PageLine>();line->block.words={"one","two","caf\xc3\xa9","four"};line->block.starts={true,false,true,false};
 s.page->elements.push_back(std::make_unique<PageElement>());s.page->elements.push_back(std::move(line));return s;}
''' + section[a:b] + r'''
int main(){auto a=fixture();assert(a.getTextFromSectionFile()=="one two caf\xc3\xa9 four");
 auto b=fixture();assert(b.getTextFromSectionFile(13,true)=="one two\ncaf\xc3\xa9");
 auto c=fixture();assert(c.getTextFromSectionFile(10,true)=="one two");
 auto d=fixture();assert(d.getTextFromSectionFile(0).empty());
 Section missing;missing.page.reset();assert(missing.getTextFromSectionFile(1024,true).empty());
}
'''
preview=(root/'src/activities/settings/TextSettingsPreview.cpp').read_text()
header=(root/'src/activities/settings/TextSettingsPreview.h').read_text()
a=header.index('struct PreviewKey {');b=header.index('// Cached engine',a)
render=r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct Rect{int x,y,width,height;};
enum class CssTextAlign{Left,Center,Right,Justify};
struct CrossPointSettings{static constexpr int BOOK_STYLE=4;};
struct{int fontPointSize=14,screenMargin=5,screenMarginVertical=0;uint8_t paragraphAlignment=0,paragraphIndentSpaces=2,wordSpacing=100;bool hyphenationEnabled=false,focusReadingEnabled=false,extraParagraphSpacing=true;
 char sdFontFamilyName[32]="First";int getReaderFontId(){return 7;}int8_t getCharacterSpacing(){return 0;}float getReaderLineCompression(){return 1;}}SETTINGS;
enum class StrId{STR_FONT_PREVIEW_TEXT};struct{const char*get(StrId){return "sample words";}}I18N;
constexpr int UI_10_FONT_ID=1;
struct FontCacheManager{int calls=0;void prewarmCache(int,const char*,int){++calls;}};
struct GfxRenderer{mutable FontCacheManager cache;mutable std::vector<std::string>body;
 int getTextHeight(int)const{return 10;}int getLineHeight(int,float)const{return 12;}
 FontCacheManager*getFontCacheManager()const{return &cache;}void drawText(int,int,int,const char*)const{}};
struct EpdFontFamily{static constexpr int REGULAR=0;};
struct BlockStyle{CssTextAlign alignment;bool textAlignDefined;};
struct TextBlock{std::string text;void render(const GfxRenderer&r,int,int,int)const{r.body.push_back(text);}};
int layouts=0;
struct ParsedText{std::string words;ParsedText(bool,bool,BlockStyle,unsigned){}
 void addWord(const std::string&w,int){if(!words.empty())words+=' ';words+=w;}
 template<class F>void layoutAndExtractLines(const GfxRenderer&,int,uint16_t,F callback,bool,int,int){++layouts;if(!words.empty())callback(std::make_unique<TextBlock>(TextBlock{words}),0);}};
namespace textsettings {
inline constexpr size_t MAX_PREVIEW_LINES=24;
''' + header[a:b] + r'''
struct PreviewLayout{PreviewLayout();~PreviewLayout();std::vector<std::unique_ptr<TextBlock>>lines;PreviewKey key;};
}
''' + preview[preview.index('namespace textsettings {'):] + r'''
int main(){GfxRenderer r;textsettings::PreviewLayout cache;Rect bounds{0,0,400,300};
 textsettings::renderPreview(r,cache,2,2,bounds,"Book preview","one two\nthree four");
 assert(layouts==2&&r.cache.calls==1&&r.body.size()==2&&r.body[0]=="one two"&&r.body[1]=="three four");
 auto* first=cache.lines[0].get();r.body.clear();textsettings::renderPreview(r,cache,2,2,bounds,"Book preview","one two\nthree four");
 assert(layouts==2&&cache.lines[0].get()==first&&r.cache.calls==1);
 strcpy(SETTINGS.sdFontFamilyName,"Second");textsettings::renderPreview(r,cache,2,2,bounds,"Book preview","one two\nthree four");assert(layouts==4&&r.cache.calls==2);
 textsettings::renderPreview(r,cache,2,2,bounds,"Book preview","changed");assert(layouts==5);
 std::string many;for(int i=0;i<100;i++)many+="line\n";textsettings::renderPreview(r,cache,2,2,bounds,"Book preview",many.c_str());assert(cache.lines.size()<=textsettings::MAX_PREVIEW_LINES);
 r.body.clear();textsettings::renderPreview(r,cache,2,2,bounds,"Preview",nullptr);assert(r.body.size()==2);
 SETTINGS.screenMarginVertical=150;r.body.clear();const int before=layouts;
 textsettings::renderPreview(r,cache,2,2,bounds,"Preview",nullptr);assert(r.body.empty()&&layouts==before);
 SETTINGS.screenMarginVertical=0;textsettings::renderPreview(r,cache,2,2,bounds,"Preview",nullptr);assert(r.body.size()==2);
}
'''
with tempfile.TemporaryDirectory() as path:
    path=Path(path)
    for name,code in [('extract',extract),('render',render)]:
        (path/(name+'.cpp')).write_text(code)
        subprocess.run(['c++','-std=c++20',str(path/(name+'.cpp')),'-o',str(path/name)],check=True)
        subprocess.run([str(path/name)],check=True)
print('Live preview keeps whole UTF-8 words, paragraphs, bounded lines, and font/text cache identity')
