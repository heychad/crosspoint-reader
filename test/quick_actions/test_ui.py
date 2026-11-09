"""Exercise the real Quick Actions activity through a minimal shared-UI host."""
from pathlib import Path
import re, subprocess, sys, tempfile
root=Path(__file__).resolve().parents[2]
source=root/'src/activities/reader/QuickActionsActivity.cpp'
keys=set()
for file in [source,root/'src/activities/reader/QuickActionsActivity.h',root/'src/personalization/ReaderShortcutLabels.h',root/'src/activities/settings/EnumSelectionActivity.cpp',root/'src/activities/settings/EnumSelectionActivity.h']:
    keys.update(re.findall(r'\bSTR_[A-Z0-9_]+\b',file.read_text()))
keys=sorted(keys)
i18n='#pragma once\nenum class StrId {'+','.join(keys)+'};\nstruct I18nStub { const char* get(StrId id)const{switch(id){'+''.join(f'case StrId::{k}:return "{k}";' for k in keys)+'}return "";}};\ninline I18nStub I18N;\n#define tr(key) I18N.get(StrId::key)\n'
ui=r'''
#pragma once
#include <cstdint>
#include <utility>
#include <I18n.h>
class GfxRenderer{};
class MappedInputManager{public:bool touch=true;bool hasTouch()const{return touch;}};
namespace freeink::ui {
struct Insets{int16_t top,right,bottom,left;};
struct ListItem{const char*label=nullptr;const char*value=nullptr;int16_t actionValue=0;};
struct Text{int maxLines=0;};
struct ListProps{void(*rowProvider)(void*,uint16_t,ListItem&)=nullptr;void*rowProviderCtx=nullptr;uint16_t count=0;int action=0,inputMask=0;Text labelText;};
constexpr int InputTouch=1;
}
struct UiScreen{
 struct Theme{freeink::ui::Text smallText;};
 freeink::ui::ListProps props;Theme style;
 void setContentMarginFromScreen(freeink::ui::Insets){} void spacer(int16_t){}
 Theme&theme(){return style;}void list(freeink::ui::ListProps p){props=p;}
};
struct MenuResult{int action=-1;uint8_t orientation=0,pageTurnOption=0;};
class UiListActivity{
protected:
 GfxRenderer&renderer;MappedInputManager&mappedInput;
 struct Nav{int selected=0;void reset(int index=0){selected=index;}}nav;
 static constexpr int ACTION_ROW=1;
 virtual int listCount()const=0;virtual const char*headerTitle()const=0;
 virtual void buildScreen(UiScreen&)=0;virtual void activateIndex(int)=0;virtual void onBackButton(){finish();}
 void resetUi(){}void requestUpdate(){}void syncListViewport(UiScreen&,freeink::ui::ListProps&){}
 void setResult(MenuResult result){selected=result.action;}void finish(){finished=true;}
public:
 bool finished=false;int selected=-1;
 UiListActivity(const char*,GfxRenderer&r,MappedInputManager&i):renderer(r),mappedInput(i){}
 virtual ~UiListActivity()=default;virtual void onEnter(){}
 int highlight()const{return nav.selected;}int count()const{return listCount();}const char*title()const{return headerTitle();}
 void tap(int index){activateIndex(index);}void back(){onBackButton();}
 freeink::ui::ListItem row(unsigned index){UiScreen screen;buildScreen(screen);freeink::ui::ListItem item;screen.props.rowProvider(screen.props.rowProviderCtx,index,item);return item;}
};
struct RenderLock{template<class T>explicit RenderLock(T&){} };
'''
settings=r'''
#pragma once
#include <array>
#include <cstdint>
struct Settings{std::array<uint8_t,5>readerQuickActions{};int saves=0;void saveToFile(){++saves;}};
inline Settings SETTINGS;
'''
main=r'''
#include <cassert>
#include <cstring>
#include <HalFrontlight.h>
#include <HalTiltSensor.h>
#include "activities/reader/QuickActionsActivity.h"
#include "activities/settings/EnumSelectionActivity.h"
#include <CrossPointSettings.h>
using A=readershortcuts::Action;
int find(UiListActivity&screen,const char*name){for(int i=0;i<screen.count();++i){if(std::strcmp(screen.row(i).label,name)==0)return i;}return -1;}
int main(){
 GfxRenderer r;MappedInputManager input;
 const uint64_t all=(uint64_t{1}<<static_cast<unsigned>(A::Count))-1;
 SETTINGS.readerQuickActions={static_cast<uint8_t>(A::Menu),static_cast<uint8_t>(A::Screenshot),0,0,0};
 QuickActionsActivity run(r,input,QuickActionsActivity::Mode::Run,all);run.onEnter();
 assert(run.count()==3);assert(std::strcmp(run.row(0).label,"STR_READER_MENU")==0);
 run.tap(0);assert(run.finished && run.selected==static_cast<int>(A::Menu));
 QuickActionsActivity edit(r,input,QuickActionsActivity::Mode::Run,all);edit.onEnter();edit.tap(edit.count()-1);
 assert(edit.count()==5);edit.tap(2);assert(edit.count()>20);
 assert(find(edit,"STR_QUICK_ACTIONS")==-1);assert(find(edit,"STR_NONE_OPT")==0);
 assert(find(edit,"STR_SAVED_ITEMS")>0);
 assert(find(edit,"STR_FILE_TRANSFER")>0);
 const int shot=find(edit,"STR_SCREENSHOT_BUTTON");assert(shot>0);edit.tap(shot);
 assert(SETTINGS.readerQuickActions[2]==static_cast<uint8_t>(A::Screenshot)&&SETTINGS.saves==1);
 assert(edit.count()==5);edit.back();assert(edit.count()==4);edit.back();assert(edit.finished);
 QuickActionsActivity onlySettings(r,input,QuickActionsActivity::Mode::Configure,all);onlySettings.onEnter();onlySettings.tap(0);onlySettings.back();assert(onlySettings.count()==5);onlySettings.back();assert(onlySettings.finished);
 Frontlight.available=false;halTiltSensor.available=false;input.touch=false;
 QuickActionsActivity limited(r,input,QuickActionsActivity::Mode::Configure,all);limited.onEnter();limited.tap(0);
 assert(find(limited,"STR_SHORTCUT_BRIGHTER")==-1);assert(find(limited,"STR_SHORTCUT_WARMER")==-1);assert(find(limited,"STR_TOUCH_READER_CONTROLS")==-1);assert(find(limited,"STR_TILT_PAGE_TURN")==-1);
 EnumSelectionActivity first(r,input,StrId::STR_QUICK_ACTIONS,readershortcuts::ACTION_LABELS,21);first.onEnter();
 assert(first.highlight()==21 && first.count()==static_cast<int>(A::Count));
 assert(std::strcmp(first.row(21).label,"STR_QUICK_ACTIONS")==0);first.tap(21);assert(first.selected==21 && first.finished);
 const int last=static_cast<int>(A::Count)-1;
 EnumSelectionActivity lastChoice(r,input,StrId::STR_QUICK_ACTIONS,readershortcuts::ACTION_LABELS,last);lastChoice.onEnter();
 assert(lastChoice.highlight()==last);lastChoice.tap(last);assert(lastChoice.selected==last);
 EnumSelectionActivity cancelled(r,input,StrId::STR_QUICK_ACTIONS,readershortcuts::ACTION_LABELS,0);cancelled.onEnter();cancelled.back();assert(cancelled.finished && cancelled.selected==-1);
 SETTINGS.readerQuickActions={};
 QuickActionsActivity chooseSaved(r,input,QuickActionsActivity::Mode::Configure,all);chooseSaved.onEnter();chooseSaved.tap(0);
 const int saved=find(chooseSaved,"STR_SAVED_ITEMS");assert(saved>0);chooseSaved.tap(saved);
 assert(SETTINGS.readerQuickActions[0]==static_cast<uint8_t>(A::SavedItems));
 QuickActionsActivity runSaved(r,input,QuickActionsActivity::Mode::Run,all);runSaved.onEnter();runSaved.tap(0);
 assert(runSaved.finished && runSaved.selected==static_cast<int>(A::SavedItems));
 SETTINGS.readerQuickActions={};
 QuickActionsActivity chooseTransfer(r,input,QuickActionsActivity::Mode::Configure,all);chooseTransfer.onEnter();chooseTransfer.tap(0);
 const int transfer=find(chooseTransfer,"STR_FILE_TRANSFER");assert(transfer>0);chooseTransfer.tap(transfer);
 assert(SETTINGS.readerQuickActions[0]==static_cast<uint8_t>(A::FileTransfer));
 QuickActionsActivity runTransfer(r,input,QuickActionsActivity::Mode::Run,all);runTransfer.onEnter();runTransfer.tap(0);
 assert(runTransfer.finished && runTransfer.selected==static_cast<int>(A::FileTransfer));
 SETTINGS.readerQuickActions={};QuickActionsActivity empty(r,input,QuickActionsActivity::Mode::Run,all);empty.onEnter();assert(empty.count()==1);assert(empty.row(20).label==nullptr);
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'activities').mkdir();(p/'components').mkdir()
    (p/'I18n.h').write_text(i18n)
    (p/'activities/UiListActivity.h').write_text(ui)
    (p/'CrossPointSettings.h').write_text(settings)
    (p/'HalFrontlight.h').write_text('#pragma once\nstruct Light{bool available=true;bool present()const{return available;}bool hasColorTemperature()const{return available;}};inline Light Frontlight;\n')
    (p/'HalTiltSensor.h').write_text('#pragma once\nstruct Tilt{bool available=true;bool isAvailable()const{return available;}};inline Tilt halTiltSensor;\n')
    (p/'components/UITheme.h').write_text('#pragma once\nstruct UITheme{struct Metrics{int topPadding=1,headerHeight=2,buttonHintsHeight=3,verticalSpacing=4;};static UITheme&getInstance(){static UITheme theme;return theme;}Metrics getMetrics()const{return{};}};\n')
    (p/'test.cpp').write_text(main)
    subprocess.run(['c++','-std=c++20','-I',str(p),'-I',str(root/'src'),'-I',sys.argv[1],str(source),str(root/'src/activities/settings/EnumSelectionActivity.cpp'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Quick Actions activity navigation, choice and filtering checks passed')
