"""Exercise production inline controls and the reader's pagination handoff."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/EpubReaderMenuActivity.cpp").read_text()
header = (root / "src/activities/reader/EpubReaderMenuActivity.h").read_text()
actions = re.search(r'enum class MenuAction\s*\{(.*?)\};', header, re.S).group(1)
data = source[source.index("namespace {"):source.index("}  // namespace") + len("}  // namespace")]

def method(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for end in range(opening, len(text)):
        depth += (text[end] == "{") - (text[end] == "}")
        if depth == 0:
            return text[start:end + 1]
    raise ValueError(signature)

methods = "\n".join(method(source, name) for name in (
    "void EpubReaderMenuActivity::applyInlineTextSetting(",
    "bool EpubReaderMenuActivity::handleInlineTextSetting(",
    "void EpubReaderMenuActivity::updateInlineTextRow("))
parent = method((root / "src/activities/reader/EpubReaderActivity.cpp").read_text(),
                "void EpubReaderActivity::applyReaderTextSettings()")
line = (root / "src/ReaderLineSpacing.h").read_text()
start = line.index("inline constexpr StrId LINE_SPACING_IDS[]")
line = line[start:line.index("};", start) + 2]

code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <cstdio>
#include <variant>
#include "I18nKeys.h"
int lockDepth=0;
struct RenderLock {RenderLock(){++lockDepth;}~RenderLock(){--lockDepth;}};
struct {const char*get(StrId){return "translated";}} I18N;
struct CrossPointSettings {
 enum {SCREEN_MARGIN_MIN=5,SCREEN_MARGIN_MAX=150,SCREEN_MARGIN_STEP=1,
 WORD_SPACING_MIN=50,WORD_SPACING_MAX=200,WORD_SPACING_STEP=25};
 uint8_t lineSpacing=0,wordSpacing=100,characterSpacing=2,textAntiAliasing=0,focusReadingEnabled=0,
 screenMargin=5,screenMarginVertical=5,paragraphAlignment=0,hyphenationEnabled=0,extraParagraphSpacing=0,
 paragraphIndentSpaces=0,embeddedStyle=0,imageRendering=0;
 int saved=0;
 bool saveToFile(){assert(lockDepth==0);++saved;return true;}
} SETTINGS;
namespace fui {struct ListItem {const char*value=nullptr;bool toggle=false,toggleChecked=false;};}
struct {void setCheckboxRow(fui::ListItem&item,bool on){item.toggle=true;item.toggleChecked=on;}} GUI;
struct Popup {
 std::function<void(int)> select;int count=0,current=0;
 template<class T>void show(T,const StrId*,int n,int index,std::function<void(int)>cb){count=n;current=index;select=std::move(cb);}
 void show(const char*,const char*const*,int n,int index,std::function<void(int)>cb){count=n;current=index;select=std::move(cb);}
};
struct IntervalResult{uint32_t value;};
struct ActivityResult{bool isCancelled=false;std::variant<IntervalResult>data;};
struct IntervalSelectionActivity{
 int initial,minimum,maximum,small,large;
 template<class... T>IntervalSelectionActivity(int,int,const char*,StrId,int i,int lo,int hi,int s,int l,T...):initial(i),minimum(lo),maximum(hi),small(s),large(l){}
};
bool allocationAllowed=true;
template<class T,class... A>std::unique_ptr<T>makeUniqueNoThrow(A&&...a){return allocationAllowed?std::make_unique<T>(std::forward<A>(a)...):nullptr;}
#define LOG_ERR(...) ((void)0)
class EpubReaderMenuActivity {public:
 enum class MenuAction {__ACTIONS__};
 bool tabbed=true;int renderer=0,mappedInput=0;char marginLabels[2][4]{};int updates=0;Popup optionPopup;std::function<void()>textSettingsChanged;
 std::function<void(const ActivityResult&)>finished;
 void startActivityForResult(std::unique_ptr<IntervalSelectionActivity>p,std::function<void(const ActivityResult&)>cb){
  assert(p->minimum==5&&p->maximum==150&&p->small==1&&p->large==5);
  finished=std::move(cb);optionPopup.count=146;optionPopup.current=p->initial-5;
  optionPopup.select=[this](int index){finished(ActivityResult{false,IntervalResult{static_cast<uint32_t>(5+index)}});};
 }
 void requestUpdate(){++updates;}
 void applyInlineTextSetting(uint8_t CrossPointSettings::*,uint8_t);
 bool handleInlineTextSetting(MenuAction);
 void updateInlineTextRow(MenuAction,fui::ListItem&);
};
struct {int loads=0;void ensureLoaded(int&){assert(lockDepth>0);++loads;}}sdFontSystem;
struct EpubReaderActivity {
 struct Section {int pageCount=90,currentPage=12;};std::unique_ptr<Section>section=std::make_unique<Section>();
 int anchors=0,cachedSpineIndex=-1,currentSpineIndex=3,cachedChapterTotalPageCount=0,nextPageNumber=0;
 int saved=0,renderer=0;bool showRenderRecoveryMessage=true;
 struct {int resets=0;void reset(){++resets;}}renderRecovery;
 void rememberCurrentContentOffset(){assert(section);++anchors;}
 bool saveBookReaderSettings(){assert(lockDepth==0&&!section);++saved;return true;}
 void applyReaderTextSettings();
};
'''.replace("__ACTIONS__", actions) + line + "\n" + data + "\n" + methods + "\n" + parent + r'''
int main(){
 using M=EpubReaderMenuActivity;using A=M::MenuAction;
 struct Expected {A action;uint8_t CrossPointSettings::*field;int count,minimum,step;};
 const Expected expected[]={
  {A::LINE_SPACING,&CrossPointSettings::lineSpacing,7,0,1},
  {A::WORD_SPACING,&CrossPointSettings::wordSpacing,7,50,25},
  {A::CHARACTER_SPACING,&CrossPointSettings::characterSpacing,5,0,1},
  {A::TEXT_AA,&CrossPointSettings::textAntiAliasing,0,0,1},
  {A::FOCUS_READING,&CrossPointSettings::focusReadingEnabled,0,0,1},
  {A::SCREEN_MARGIN,&CrossPointSettings::screenMargin,146,5,1},
  {A::VERTICAL_MARGIN,&CrossPointSettings::screenMarginVertical,146,5,1},
  {A::ALIGNMENT,&CrossPointSettings::paragraphAlignment,5,0,1},
  {A::HYPHENATION,&CrossPointSettings::hyphenationEnabled,0,0,1},
  {A::PARAGRAPH_SPACING,&CrossPointSettings::extraParagraphSpacing,0,0,1},
  {A::PARAGRAPH_INDENT,&CrossPointSettings::paragraphIndentSpaces,6,0,1},
  {A::EMBEDDED_STYLE,&CrossPointSettings::embeddedStyle,0,0,1},
  {A::IMAGES,&CrossPointSettings::imageRendering,4,0,1}};
 for(const auto&e:expected){
  SETTINGS={};M menu;EpubReaderActivity reader;
  menu.textSettingsChanged=[&]{reader.applyReaderTextSettings();};
  const auto*control=textControl(e.action);assert(control&&control->field==e.field);
  const auto initial=SETTINGS.*e.field;
  assert(menu.handleInlineTextSetting(e.action));
  if(e.count){
   assert(SETTINGS.*e.field==initial&&SETTINGS.saved==0&&reader.anchors==0);
   assert(menu.optionPopup.count==e.count);
   menu.optionPopup.select(-1);menu.optionPopup.select(e.count);
   assert(SETTINGS.*e.field==initial&&SETTINGS.saved==0);
   for(int index=0;index<e.count;++index){
    menu.optionPopup.select(index);
    assert(SETTINGS.*e.field==e.minimum+index*e.step);
    fui::ListItem row;menu.updateInlineTextRow(e.action,row);assert(row.value&&!row.toggle);
   }
  }else{
   assert(SETTINGS.*e.field==1);
   fui::ListItem row;menu.updateInlineTextRow(e.action,row);assert(row.toggle&&row.toggleChecked);
   menu.handleInlineTextSetting(e.action);assert(SETTINGS.*e.field==0);
  }
  assert(!reader.section&&reader.anchors==1&&reader.cachedSpineIndex==3);
  assert(reader.cachedChapterTotalPageCount==90&&reader.nextPageNumber==12);
  assert(reader.saved==SETTINGS.saved&&reader.saved==sdFontSystem.loads);
  const int saves=SETTINGS.saved;
  menu.applyInlineTextSetting(e.field,SETTINGS.*e.field);assert(SETTINGS.saved==saves);
  menu.tabbed=false;assert(!menu.handleInlineTextSetting(e.action)&&SETTINGS.saved==saves);
  sdFontSystem.loads=0;
 }
 M menu;assert(!menu.handleInlineTextSetting(A::GO_HOME));
 SETTINGS={};menu.handleInlineTextSetting(A::VERTICAL_MARGIN);
 menu.finished(ActivityResult{true,IntervalResult{100}});assert(SETTINGS.screenMarginVertical==5);
 menu.optionPopup.select(95);assert(SETTINGS.screenMarginVertical==100&&SETTINGS.screenMargin==5);
 fui::ListItem h,v;menu.updateInlineTextRow(A::SCREEN_MARGIN,h);menu.updateInlineTextRow(A::VERTICAL_MARGIN,v);
 assert(std::string(h.value)=="5"&&std::string(v.value)=="100");
 allocationAllowed=false;assert(menu.handleInlineTextSetting(A::SCREEN_MARGIN));assert(SETTINGS.screenMargin==5);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-I", str(root / "lib/I18n"),
                    str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("All 12 inline controls, invalid choices, unchanged values, and pagination handoff passed")
