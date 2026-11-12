"""Exercise the production build policy and indexing picker handoff."""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/activities/reader/EpubReaderActivity.cpp').read_text()
a=source.index('  // Full-section mode finishes the chapter here;')
b=source.index('  if (!section->isBuilding() && section->pageCount > 0',a)
loops=source[a:b]
a=source.index('    case EpubReaderMenuActivity::MenuAction::INDEXING_METHOD:')
b=source.index('    case EpubReaderMenuActivity::MenuAction::RENDER_MODE:',a)
action=source[a:b]
policy=re.search(r'const bool needsFullBuild = ([^;]+);',source).group(1)
code=r'''
#include <cassert>
#include <functional>
#include <memory>
#include <variant>
#define LOG_ERR(...) ((void)0)
#define tr(x) 0
enum class StrId {STR_INDEXING_INCREMENTAL,STR_INDEXING_FULL_SECTION,STR_INDEXING_METHOD};
struct CrossPointSettings {enum{INDEXING_INCREMENTAL,INDEXING_FULL_SECTION,INDEXING_METHOD_COUNT};};
struct {unsigned char indexingMethod=0;}SETTINGS;
struct RenderLock{};
struct MenuResult{int action;};struct ActivityResult{bool isCancelled=false;std::variant<std::monostate,MenuResult>data;};
struct EnumSelectionActivity{template<class...T>EnumSelectionActivity(T&&...){}};
bool allocation=true;
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...args){if(!allocation)return nullptr;return std::make_unique<T>(std::forward<A>(args)...);}
struct EpubReaderMenuActivity{enum class MenuAction{INDEXING_METHOD};};
struct {void drawPopup(int&,int){}}GUI;
constexpr int BUILD_PAGES_PER_CHUNK=2;
struct Section {
 bool partial=true,building=false,complete=false,startOk=true,stepOk=true;int currentPage=1,pageCount=5,limit=20,starts=0,steps=0;
 bool isPartial(){return partial;}bool isBuilding(){return building;}bool isBuildComplete(){return complete;}
 bool startBuild(int){++starts;building=startOk;return startOk;}
 bool buildSomeMore(int pages){++steps;if(!stepOk)return false;pageCount+=pages;if(pageCount>=limit){pageCount=limit;complete=true;partial=false;building=false;}return true;}
};
struct Reader {
 std::unique_ptr<Section>section=std::make_unique<Section>();int renderer=0,mappedInput=0,renderSpec=0,pagesUntilFullRefresh=0,errors=0,roomCalls=0,menus=0,saves=0;
 bool room=true;std::function<void(const ActivityResult&)>callback;
 bool makeRoomForChapterBuild(){++roomCalls;return room;}bool tryRenderRecovery(){return false;}void showBuildError(int=0){++errors;}
 void openReaderMenu(){++menus;}void saveBookReaderSettingsLocked(){++saves;}
 template<class F>void startActivityForResult(std::unique_ptr<EnumSelectionActivity>,F f){callback=f;}
 void complete(bool cancel,int value){auto f=callback;f({cancel,MenuResult{value}});}
 void run(bool fullSectionIndexing){
''' + loops + r'''
 }
 void choose(EpubReaderMenuActivity::MenuAction action){switch(action){
''' + action + r'''
 }}
};
bool needsFull(bool fullSectionIndexing,bool pendingPercentJump){return ''' + policy + r''';}
int main(){
 assert(!needsFull(false,false)&&needsFull(true,false)&&needsFull(false,true));
 Reader incremental;incremental.run(false);assert(incremental.section->steps==0&&incremental.section->currentPage==1);
 Reader full;full.run(true);assert(full.section->complete&&full.section->starts==1&&full.section->currentPage==1&&full.roomCalls==1);
 Reader target;target.section->currentPage=8;target.run(false);assert(target.section->pageCount==9&&!target.section->complete&&target.section->currentPage==8);
 Reader building;building.section->partial=false;building.section->building=true;building.run(true);assert(building.section->complete);
 Reader cached;cached.section->partial=false;cached.section->complete=true;cached.run(true);assert(cached.section->steps==0&&cached.roomCalls==0);
 Reader refused;refused.room=false;refused.run(true);assert(!refused.section&&refused.errors==1);
 Reader failed;failed.section->stepOk=false;failed.run(true);assert(!failed.section&&failed.errors==1);
 Reader menu;menu.choose(EpubReaderMenuActivity::MenuAction::INDEXING_METHOD);menu.complete(true,1);assert(menu.saves==0&&SETTINGS.indexingMethod==0);
 menu.choose(EpubReaderMenuActivity::MenuAction::INDEXING_METHOD);menu.complete(false,1);assert(menu.saves==1&&SETTINGS.indexingMethod==1);
 menu.choose(EpubReaderMenuActivity::MenuAction::INDEXING_METHOD);menu.complete(false,2);assert(menu.saves==1&&SETTINGS.indexingMethod==1);
 allocation=false;menu.choose(EpubReaderMenuActivity::MenuAction::INDEXING_METHOD);assert(menu.menus==4);
}
'''
with tempfile.TemporaryDirectory() as path:
    path=Path(path);(path/'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Indexing policy, partial-cache continuation, and picker handoff passed')
