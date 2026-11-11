"""Exercise production menu grouping, tab transitions, and flat-list preservation."""
from pathlib import Path
import re
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'src/activities/reader/EpubReaderMenuActivity.cpp').read_text()
header = (ROOT / 'src/activities/reader/EpubReaderMenuActivity.h').read_text()

def method(signature):
    start = source.index(signature)
    if signature == 'void EpubReaderMenuActivity::buildMenuItems(':
        return source[start:source.index('\nvoid EpubReaderMenuActivity::toggleBluetooth()', start)]
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end+1]
    raise ValueError(signature)

names = ('EpubReaderMenuActivity::Tab EpubReaderMenuActivity::tabForAction(',
         'void EpubReaderMenuActivity::buildMenuItems(',
         'void EpubReaderMenuActivity::buildMenuRowItems()',
         'void EpubReaderMenuActivity::rebuildVisibleItems()',
         'void EpubReaderMenuActivity::onTabAction(',
         'void EpubReaderMenuActivity::stepTab(',
         'bool EpubReaderMenuActivity::handleButtons()')
methods = '\n'.join(method(name) for name in names)
actions = re.search(r'enum class MenuAction\s*\{(.*?)\};', header, re.S).group(1)
control_data = source[source.index('namespace {'):source.index('}  // namespace') + len('}  // namespace')]
ids = sorted(set(re.findall(r'StrId::(STR_[A-Z0-9_]+)', methods + control_data)))
capacity = re.search(r'MAX_MENU_ITEMS = (\d+)', header).group(1)
code = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>
enum class StrId { __IDS__ };
struct {const char* get(StrId){return "label";}} I18N;
struct CrossPointSettings {
 enum {SLIDE_SWITCH_NIGHT_MODE,SCREEN_MARGIN_MIN=5,SCREEN_MARGIN_MAX=40,SCREEN_MARGIN_STEP=5,
 WORD_SPACING_MIN=50,WORD_SPACING_MAX=200,WORD_SPACING_STEP=25};
 uint8_t lineSpacing=0,wordSpacing=100,characterSpacing=2,textAntiAliasing=0,focusReadingEnabled=0,
 screenMargin=5,paragraphAlignment=0,hyphenationEnabled=0,extraParagraphSpacing=0,
 paragraphIndentSpaces=0,embeddedStyle=0,imageRendering=0;
};
constexpr StrId LINE_SPACING_IDS[7]{};
struct {char dictionaryName[2]={'x',0};struct {char peerAddr[2]={'x',0};} ble;bool moveFinishedToReadFolder=false;} SETTINGS;
struct BoardConfig {static bool hasHomeKey(){return true;}};
struct {bool present(){return true;}} Frontlight;
bool slideSwitchOwns(int){return false;} bool slideSwitchOwnsLight(){return false;}
namespace bleturner {struct Status{bool compiledIn=true;};Status status(){return{};}}
namespace pluginevents {enum class Event{ONE,COUNT};bool anySubscriber(Event){return true;}}
struct {bool hasCredentials(){return true;}} KOREADER_STORE;
namespace fui {struct ListItem {const char* label=nullptr;int16_t actionValue=-1;};}
struct Nav {int selected=0;bool followOnBuild=false;};
struct RenderLock {RenderLock()=default;~RenderLock(){}template<class T>explicit RenderLock(T&){};};
struct MappedInputManager {enum class Button{Back,Confirm};bool back=false,confirm=false;bool wasReleased(Button b){return b==Button::Back?back:confirm;}};
class EpubReaderMenuActivity {public:
 enum class MenuAction { __ACTIONS__ };
 enum class Tab:uint8_t{Font,Layout,More,Location,Settings,Count};
 enum class Pane:uint8_t{Root,FontFamily,FontSize};Pane pane=Pane::Root;
 struct MenuItem {MenuAction action;StrId labelId;};
 static constexpr size_t MAX_MENU_ITEMS=__CAPACITY__;
 static Tab tabForAction(MenuAction);
 static void buildMenuItems(std::vector<MenuItem>&,bool,bool,bool,bool=false,bool=false,bool=false,bool=false);
 bool tabbed=true;Tab tab=Tab::Font;std::vector<MenuItem> menuItems;
 uint8_t visibleIndexes[MAX_MENU_ITEMS]{};int visibleCount=0;fui::ListItem menuRowItems[MAX_MENU_ITEMS]{};
 Nav nav;std::array<Nav,5> navs{};int activations=0,activated=-1;bool cancelled=false;
 struct {bool open=false;bool isActive(){return open;}} optionPopup;
 struct {void clearTapFlash(){}} app;
 MappedInputManager mappedInput;
 Nav& activeNav(){return tabbed?navs[activeTab()]:nav;}
 int activeTab()const{return static_cast<int>(tab);} int tabCount()const{return 5;}
 int ringPos()const{return navs[activeTab()].selected;}
 MenuAction visibleAction(int row)const{return menuItems[visibleIndexes[row]].action;}
 void requestUpdate(){};void closeCancelled(){cancelled=true;}
 void closeFontPane(){pane=Pane::Root;}
 void activateIndex(int index){++activations;activated=index;}
 void buildMenuRowItems();void rebuildVisibleItems();void onTabAction(int);void stepTab(int);bool handleButtons();
};
'''.replace('__IDS__', ','.join(ids)).replace('__ACTIONS__', actions).replace('__CAPACITY__', capacity)
reader = (ROOT / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
start = reader.index('    case EpubReaderMenuActivity::MenuAction::TEXT_SETTINGS:')
end = reader.index('    case EpubReaderMenuActivity::MenuAction::BLUETOOTH:', start)
handoff = reader[start:end]
code += control_data + '\n' + methods
code += r'''
struct ActivityResult {};
struct TextSettingsActivity {
 enum class Tab{Family,Size,Layout,Style};Tab initial;
 TextSettingsActivity(int&,MappedInputManager&,int*,Tab tab):initial(tab){}
};
bool allocationAvailable=true;
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... args){return allocationAvailable?std::make_unique<T>(std::forward<A>(args)...):nullptr;}
#define LOG_ERR(...) ((void)0)
struct Reader {
 int renderer=0,saved=0,reopened=0;MappedInputManager mappedInput;
 struct {int value=0;int& registry(){return value;}} sdFontSystem;
 struct Section {int pageCount=0,currentPage=0;};std::unique_ptr<Section> section;
 int cachedSpineIndex=0,currentSpineIndex=0,cachedChapterTotalPageCount=0,nextPageNumber=0;
 TextSettingsActivity::Tab opened=TextSettingsActivity::Tab::Family;
 void rememberCurrentContentOffset(){}void saveBookReaderSettings(){++saved;}void openReaderMenu(){++reopened;}
 template<class Callback>void startActivityForResult(std::unique_ptr<TextSettingsActivity> child,Callback cb){opened=child->initial;cb(ActivityResult{});}
 void execute(EpubReaderMenuActivity::MenuAction action){switch(action){
''' + handoff + r'''
 default:break;}}
};
'''
code += r'''
int main(){
 using M=EpubReaderMenuActivity;using A=M::MenuAction;using T=M::Tab;
 assert(textControl(A::LINE_SPACING)!=nullptr && textControl(A::GO_HOME)==nullptr);
 Reader reader;
 reader.execute(A::FONT_SIZE);assert(reader.opened==TextSettingsActivity::Tab::Size && reader.saved==1 && reader.reopened==1);
 reader.execute(A::LAYOUT_SETTINGS);assert(reader.opened==TextSettingsActivity::Tab::Layout);
 reader.execute(A::STYLE_SETTINGS);assert(reader.opened==TextSettingsActivity::Tab::Style);
 reader.execute(A::TEXT_SETTINGS);assert(reader.opened==TextSettingsActivity::Tab::Family && reader.saved==4);
 allocationAvailable=false;reader.execute(A::LAYOUT_SETTINGS);assert(reader.saved==4 && reader.reopened==5);
 M m;M::buildMenuItems(m.menuItems,true,true,true,true,false,false,true);m.rebuildVisibleItems();
 assert(m.visibleCount==7 && m.visibleAction(0)==A::TEXT_SETTINGS && m.visibleAction(1)==A::FONT_SIZE);
 std::array<int,5> counts{};
 for(const auto& row:m.menuItems)++counts[static_cast<int>(M::tabForAction(row.action))];
 for(int count:counts)assert(count>0);
 assert(m.menuItems.size()<=M::MAX_MENU_ITEMS);
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) && defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT)
 assert(m.menuItems.size()==M::MAX_MENU_ITEMS);
#endif
 m.navs[0].selected=2;m.stepTab(1);assert(m.tab==T::Layout && m.visibleCount==counts[1]);
 bool hasImages=false;for(int i=0;i<m.visibleCount;++i)hasImages|=m.visibleAction(i)==A::IMAGES;assert(hasImages);
 m.stepTab(-1);assert(m.tab==T::Font && m.activeNav().selected==2);
 m.mappedInput.confirm=true;assert(m.handleButtons() && m.activated==1);
 m.activeNav().selected=0;assert(m.handleButtons() && m.tab==T::Layout && m.activations==1);
 m.optionPopup.open=true;m.onTabAction(3);assert(m.tab==T::Layout);m.optionPopup.open=false;
 m.onTabAction(-1);m.onTabAction(5);assert(m.tab==T::Layout);
 m.onTabAction(0);m.stepTab(-1);assert(m.tab==T::Settings);m.stepTab(1);assert(m.tab==T::Font);
 m.mappedInput.confirm=false;m.mappedInput.back=true;assert(m.handleButtons() && m.cancelled);
 M flat;flat.tabbed=false;M::buildMenuItems(flat.menuItems,true,true,true);flat.rebuildVisibleItems();
 assert(flat.visibleCount==static_cast<int>(flat.menuItems.size()));
 for(int i=0;i<flat.visibleCount;++i){assert(flat.visibleIndexes[i]==i && flat.menuRowItems[i].actionValue==i);assert(textControl(flat.visibleAction(i))==nullptr);}
 flat.nav.selected=3;flat.mappedInput.confirm=true;assert(flat.handleButtons() && flat.activated==3);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'tabs.cpp').write_text(code)
    for flags in ([], ['-DCROSSPOINT_EXPERIMENTAL_COMPLETION', '-DCROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT', '-DCROSSPOINT_EXPERIMENTAL_STATS_RESTORE']):
        subprocess.run(['c++','-std=c++20','-Wall','-Wextra','-Werror',*flags,str(path/'tabs.cpp'),'-o',str(path/'tabs')],check=True)
        subprocess.run([str(path/'tabs')],check=True)
print('Tabbed menu grouping, remembered rows, wrap, popup ownership, and flat-list behavior passed')
