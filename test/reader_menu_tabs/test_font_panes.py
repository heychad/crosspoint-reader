"""Exercise production in-menu font panes with a mutable font registry."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/activities/reader/EpubReaderMenuActivity.cpp").read_text()

def method(signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end+1]
    raise ValueError(signature)

methods = "\n".join(method(signature) for signature in (
    "int EpubReaderMenuActivity::listCount() const",
    "int EpubReaderMenuActivity::currentFontRow() const",
    "bool EpubReaderMenuActivity::openFontPane(",
    "void EpubReaderMenuActivity::closeFontPane()",
    "void EpubReaderMenuActivity::provideFontRow(",
    "void EpubReaderMenuActivity::selectFontRow(",
    "bool EpubReaderMenuActivity::handleCustomInput()",
    "bool EpubReaderMenuActivity::handleButtons()",
    "void EpubReaderMenuActivity::onExit()",
    "void EpubReaderMenuActivity::applyInlineTextSetting("))
code = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>
#include "I18nKeys.h"
struct {const char*get(StrId){return "label";}}I18N;
#define tr(key) I18N.get(StrId::key)
#define LOG_ERR(...) ((void)0)
struct RenderLock {template<class...T>RenderLock(T&&...) {}};
struct CrossPointSettings {
 static constexpr int BUILTIN_FONT_COUNT=1;
 char sdFontFamilyName[32]{};uint8_t fontFamily=0,fontPointSize=14;int saves=0;
 bool saveToFile(){++saves;return true;}
} SETTINGS;
struct Family {std::string name;};
struct Registry {
 std::vector<Family> families;
 int getFamilyCount()const{return families.size();}
 const auto&getFamilies()const{return families;}
};
struct FontSystem {
 Registry data;bool dirty=false,fail=false;
 Registry&registry(){return data;}
 bool refreshIfDirty(){dirty=fail;return !fail;}
 bool registryNeedsRefresh(){return dirty;}
} sdFontSystem;
std::vector<uint8_t>readerFontPointSizes(Registry*,const char*name){return name[0]?std::vector<uint8_t>{10,17,23}:std::vector<uint8_t>{12,14,16,18};}
uint8_t snapToNearestPointSize(const std::vector<uint8_t>&sizes,uint8_t value){
 auto result=sizes.front();for(auto size:sizes)if(std::abs(int(size)-value)<std::abs(int(result)-value))result=size;return result;
}
namespace fui {struct ListItem {const char*label=nullptr,*value=nullptr;int16_t actionValue=0;bool enabled=true;};}
struct Nav {int selected=0;void reset(int index){selected=index;}};
struct UiTabListActivity {Nav nav;Nav&activeNav(){return nav;}void onExit(){}};
using UiListActivity=UiTabListActivity;
struct MappedInputManager {enum class Button{Back,Confirm};bool back=false,confirm=false;bool wasReleased(Button b){return b==Button::Back?back:confirm;}};
class EpubReaderMenuActivity:public UiTabListActivity {public:
 enum class Pane:uint8_t{Root,FontFamily,FontSize};Pane pane=Pane::Root;
 enum class MenuAction{TEXT_SETTINGS,FONT_SIZE,OTHER};MenuAction selectedAction=MenuAction::OTHER;
 bool tabbed=true,cancelled=false;int visibleCount=7,selectedFontRow=0,updates=0;
 Nav rootFontNav;std::vector<uint8_t>fontSizes;char fontSizeLabel[12]{};
 MappedInputManager mappedInput;
 struct {int cleared=0;void clearTapFlash(){++cleared;}}app;
 struct Popup {
  int errors=0;template<class...T>void showMessage(T&&...){++errors;}
  template<class T>bool handleInput(MappedInputManager&,T){return false;}
 }optionPopup;
 std::function<void()>textSettingsChanged;
 void requestUpdate(){++updates;}void closeCancelled(){cancelled=true;}
 int ringPos(){return nav.selected;}
 void stepTab(int){closeFontPane();}void activateIndex(int index){selectFontRow(index);}
 MenuAction visibleAction(int){return MenuAction::OTHER;}void onExit();
 int listCount()const;int currentFontRow()const;bool openFontPane(Pane);
 void closeFontPane();void selectFontRow(int);static void provideFontRow(void*,uint16_t,fui::ListItem&);
 bool handleCustomInput();bool handleButtons();void applyInlineTextSetting(uint8_t CrossPointSettings::*,uint8_t);
};
''' + methods + r'''
int main(){
 using M=EpubReaderMenuActivity;using P=M::Pane;
 for(int i=0;i<80;++i)sdFontSystem.data.families.push_back({"Font"+std::to_string(i)});
 M menu;int changes=0;menu.textSettingsChanged=[&]{++changes;};menu.nav.selected=2;
 assert(menu.openFontPane(P::FontFamily));assert(menu.listCount()==81&&menu.nav.selected==1);
 fui::ListItem row;menu.provideFontRow(&menu,75,row);assert(std::string(row.label)=="Font74");
 menu.selectFontRow(75);assert(std::string(SETTINGS.sdFontFamilyName)=="Font74"&&changes==1);
 menu.selectFontRow(75);assert(changes==1&&SETTINGS.saves==1);
 menu.selectFontRow(-1);menu.selectFontRow(81);assert(changes==1);
 menu.mappedInput.back=true;assert(menu.handleButtons());assert(menu.pane==P::Root&&!menu.cancelled&&menu.nav.selected==2);
 menu.mappedInput.back=false;
 assert(menu.openFontPane(P::FontSize));assert(menu.listCount()==3&&menu.nav.selected==2);
 menu.provideFontRow(&menu,2,row);assert(std::string(row.label)=="23 pt");
 menu.nav.selected=3;menu.mappedInput.confirm=true;assert(menu.handleButtons());
 assert(SETTINGS.fontPointSize==23&&changes==2&&menu.selectedFontRow==2);
 menu.onExit();assert(menu.selectedAction==M::MenuAction::FONT_SIZE);
 menu.mappedInput.confirm=false;menu.closeFontPane();assert(menu.nav.selected==2);
 assert(menu.openFontPane(P::FontFamily));menu.selectFontRow(0);assert(!SETTINGS.sdFontFamilyName[0]&&changes==3);
 menu.closeFontPane();assert(menu.openFontPane(P::FontSize));assert(menu.listCount()==4);
 sdFontSystem.dirty=true;menu.selectFontRow(0);assert(changes==3);
 assert(menu.handleCustomInput());assert(menu.pane==P::Root&&menu.nav.selected==2);
 sdFontSystem.fail=true;assert(!menu.openFontPane(P::FontFamily));assert(menu.pane==P::Root&&menu.optionPopup.errors==1);
 sdFontSystem.fail=false;assert(menu.openFontPane(P::FontFamily));
 sdFontSystem.data.families[0].name=std::string(40,'x');
 menu.provideFontRow(&menu,1,row);assert(!row.enabled);
 menu.selectFontRow(1);assert(changes==3&&!SETTINGS.sdFontFamilyName[0]);
 menu.onExit();assert(menu.selectedAction==M::MenuAction::TEXT_SETTINGS);
 menu.closeFontPane();menu.mappedInput.back=true;menu.handleButtons();assert(menu.cancelled);
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-I", str(root / "lib/I18n"),
                    str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("Font panes, large registries, sparse sizes, Back navigation, and dirty-registry guards passed")
