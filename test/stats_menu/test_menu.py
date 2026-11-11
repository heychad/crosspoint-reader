"""Exercise production EPUB menu construction and the statistics action handoff."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
menu_source = (root / "src/activities/reader/EpubReaderMenuActivity.cpp").read_text()
reader_source = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
header = (root / "src/activities/reader/EpubReaderMenuActivity.h").read_text()
menu = menu_source[menu_source.index("void EpubReaderMenuActivity::buildMenuItems("):menu_source.index("\nvoid EpubReaderMenuActivity::toggleBluetooth()")]
more = reader_source[reader_source.index("void EpubReaderActivity::buildMoreActions()"):reader_source.index("\nstd::string EpubReaderActivity::moreRowName")]
start = reader_source.index("    case EpubReaderMenuActivity::MenuAction::READING_STATISTICS:")
end = reader_source.index("    case EpubReaderMenuActivity::MenuAction::", start + 10)
action = reader_source[start:end]
saved_start = reader_source.index("    case EpubReaderMenuActivity::MenuAction::SAVED_ITEMS:")
saved_end = reader_source.index("    case EpubReaderMenuActivity::MenuAction::", saved_start + 10)
saved_action = reader_source[saved_start:saved_end]
library_start = reader_source.index("    case EpubReaderMenuActivity::MenuAction::LIBRARY:")
library_end = reader_source.index("    case EpubReaderMenuActivity::MenuAction::", library_start + 10)
library_action = reader_source[library_start:library_end]
completion_start = reader_source.index("    case EpubReaderMenuActivity::MenuAction::TOGGLE_COMPLETION:")
completion_end = reader_source.index("    case EpubReaderMenuActivity::MenuAction::", completion_start + 10)
completion_action = reader_source[completion_start:completion_end]
quick_start = reader_source.index("    case EpubReaderMenuActivity::MenuAction::QUICK_ACTIONS:")
quick_end = reader_source.index("    case EpubReaderMenuActivity::MenuAction::", quick_start + 10)
quick_action = reader_source[quick_start:quick_end]
ids = sorted(set(re.findall(r"StrId::(STR_[A-Z_]+)", menu)))
actions = sorted(set(re.findall(r"MenuAction::([A-Z_]+)", menu)))
capacity = int(re.search(r"MAX_MENU_ITEMS\s*=\s*(\d+)", header).group(1))
code = r'''
#include <algorithm>
#include <cassert>
#include <vector>
#include "util/ReadFolder.h"
enum class StrId { ''' + ",".join(ids) + r''' };
struct CrossPointSettings {enum {SLIDE_SWITCH_NIGHT_MODE=1};};
struct BoardConfig {static bool hasHomeKey(){return true;}};
struct Settings {char dictionaryName[2]={'x',0};struct {char peerAddr[2]={'x',0};} ble;bool moveFinishedToReadFolder=false;} SETTINGS;
struct RenderLock {};
struct ReaderStats {bool known=true, completed=false;bool completionStatus(bool& out){out=completed;return known;}};
struct {bool present()const{return true;}} Frontlight;
bool slideSwitchOwns(int){return false;} bool slideSwitchOwnsLight(){return false;}
namespace bleturner {struct Status{bool compiledIn=true;};Status status(){return{};}}
namespace pluginevents {enum class Event{ANY,COUNT};bool anySubscriber(Event){return true;}}
struct {bool hasCredentials()const{return true;}} KOREADER_STORE;
struct {bool hasClippings()const{return true;}} CLIPPINGS;
class EpubReaderMenuActivity {public:
 enum class MenuAction {''' + ",".join(actions) + r'''};
 struct MenuItem {MenuAction action;StrId labelId;};
 static constexpr int MAX_MENU_ITEMS=''' + str(capacity) + r''';
 static void buildMenuItems(std::vector<MenuItem>&,bool,bool,bool,bool=false,bool=false,bool=false,bool=false);
};
''' + menu + r'''
namespace readershortcuts {enum class Action {ReadingStatistics,Library,QuickActions};}
struct Manager {int calls=0; bool allocationSucceeds=true, pending=false;
 void goToSavedItems(){++calls;pending=allocationSucceeds;}
} activityManager;
class EpubReaderActivity {public:
 std::vector<int> currentPageFootnotes{1},cachedBookmarks{1};
 std::vector<EpubReaderMenuActivity::MenuItem> moreItems;
 bool opened=false,libraryOpened=false,quickOpened=false,attempted=false,target=false;int repaints=0;
 ReaderStats readingStats;
 std::string bookPath="/book.epub";
 void handleCompletionAttempt(bool recovery,bool completed){assert(!recovery);attempted=true;target=completed;}
 void requestUpdate(){++repaints;}
 bool handleReaderShortcut(readershortcuts::Action action){opened=action==readershortcuts::Action::ReadingStatistics;libraryOpened=action==readershortcuts::Action::Library;quickOpened=action==readershortcuts::Action::QuickActions;return true;}
 void buildMoreActions();
 void dispatch(EpubReaderMenuActivity::MenuAction action){switch(action){
''' + action + saved_action + library_action + completion_action + quick_action + r'''
 default: break;}}
};
''' + more + r'''
int main(){
 using Menu=EpubReaderMenuActivity;using MA=Menu::MenuAction;
 std::vector<Menu::MenuItem> rows;Menu::buildMenuItems(rows,true,true,true);
 assert(rows.size()<=Menu::MAX_MENU_ITEMS);
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
 Menu::buildMenuItems(rows,true,true,true,true,false);
 assert(rows.size()==Menu::MAX_MENU_ITEMS-3);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION && item.labelId==StrId::STR_MARK_FINISHED;})==1);
 Menu::buildMenuItems(rows,true,true,true,false,false);
 assert(std::none_of(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION;}));
 SETTINGS.moveFinishedToReadFolder=true;
 Menu::buildMenuItems(rows,true,true,true,true,false);
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION;})==1);
#else
 assert(std::none_of(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION;}));
#endif
 Menu::buildMenuItems(rows,true,true,true,true,false,true);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION;})==1);
 Menu::buildMenuItems(rows,true,true,true,true,true);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION && item.labelId==StrId::STR_MARK_UNFINISHED;})==1);
 SETTINGS.moveFinishedToReadFolder=false;
#endif
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::READING_STATISTICS && item.labelId==StrId::STR_READING_STATISTICS;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::READER_BARS && item.labelId==StrId::STR_READER_BARS;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::HOME_BUTTON && item.labelId==StrId::STR_HOME_BUTTON;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::CONTROLS && item.labelId==StrId::STR_CAT_CONTROLS;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::SAVED_ITEMS && item.labelId==StrId::STR_SAVED_ITEMS;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::LIBRARY && item.labelId==StrId::STR_LIBRARY;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::RENDER_MODE && item.labelId==StrId::STR_EPUB_RENDER_MODE;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::FILE_BROWSER && item.labelId==StrId::STR_BROWSE_FILES;})==1);
 assert(std::count_if(rows.begin(),rows.end(),[](const auto& item){return item.action==MA::FILE_TRANSFER && item.labelId==StrId::STR_FILE_TRANSFER;})==1);
 EpubReaderActivity reader;reader.buildMoreActions();
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::TOGGLE_COMPLETION;})==1);
 reader.dispatch(MA::TOGGLE_COMPLETION);assert(reader.attempted && reader.target);
 reader.attempted=false;reader.readingStats.completed=true;
 reader.dispatch(MA::TOGGLE_COMPLETION);assert(reader.attempted && !reader.target);
 reader.attempted=false;reader.readingStats.completed=false;SETTINGS.moveFinishedToReadFolder=true;
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
 reader.dispatch(MA::TOGGLE_COMPLETION);assert(reader.attempted && reader.target);
#else
 reader.dispatch(MA::TOGGLE_COMPLETION);assert(!reader.attempted);
#endif
 SETTINGS.moveFinishedToReadFolder=false;
 reader.attempted=false;reader.readingStats.known=false;
 reader.dispatch(MA::TOGGLE_COMPLETION);assert(!reader.attempted);
#endif
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::READING_STATISTICS;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::READER_BARS;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::HOME_BUTTON;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::CONTROLS;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::QUICK_ACTIONS;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::SAVED_ITEMS;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::LIBRARY;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::RENDER_MODE;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::FILE_BROWSER;})==1);
 assert(std::count_if(reader.moreItems.begin(),reader.moreItems.end(),[](const auto& item){return item.action==MA::FILE_TRANSFER;})==1);
 reader.dispatch(MA::READING_STATISTICS);assert(reader.opened);
 reader.dispatch(MA::LIBRARY);assert(reader.libraryOpened && reader.repaints==1);
 reader.dispatch(MA::QUICK_ACTIONS);assert(reader.quickOpened && reader.repaints==2);
 reader.dispatch(MA::SAVED_ITEMS);assert(activityManager.calls==1 && activityManager.pending && reader.repaints==3);
 activityManager.allocationSucceeds=false;activityManager.pending=false;
 reader.dispatch(MA::SAVED_ITEMS);assert(activityManager.calls==2 && !activityManager.pending && reader.repaints==4);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    for flags in ([], ["-DCROSSPOINT_EXPERIMENTAL_COMPLETION", "-DCROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT"],
                  ["-DCROSSPOINT_EXPERIMENTAL_COMPLETION", "-DCROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT", "-DCROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION"]):
        subprocess.run(["c++", "-std=c++20", "-I", str(root / "src"), *flags, str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        subprocess.run([str(path / "test")], check=True)
print("EPUB reader menu and More panel expose statistics, Library, and Saved Items")
