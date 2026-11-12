"""Exercise the production clipping initializer and painter under queued-render timing."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/activities/reader/ClipSelectionActivity.cpp').read_text()
def method(signature):
 start=s.index(signature);opening=s.index('{',start);depth=0
 for end in range(opening,len(s)):
  depth+=(s[end]=='{')-(s[end]=='}')
  if depth==0:return s[start:end+1]
 raise ValueError(signature)
enter=method('void ClipSelectionActivity::onEnter(')
render=method('void ClipSelectionActivity::render(')
program=r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#define LOG_ERR(...) ((void)0)
#define tr(x) "label"
enum class StrId{STR_BACK,STR_DICT_LOW_MEMORY};
std::mutex renderMutex,initMutex;std::condition_variable changed;
thread_local int depth=0;std::atomic<int>painted{0},fontReads{0};bool extracting=false,finishInit=false;
struct RenderLock{std::unique_lock<std::mutex>lock;RenderLock():lock(renderMutex){++depth;}~RenderLock(){--depth;}};
struct Activity{void onEnter(){}};
struct Rect{int x=0,y=0,width=480,height=800;};
struct FontCache{struct Scope{void endScanAndPrewarm(){}};Scope createPrewarmScope(){return{};}};
struct Renderer{FontCache fonts;int getLineHeight(int id){assert(depth==1&&id==2);return 18;}
 void clearScreen(){}auto getClipRect(){return std::array<int,4>{0,0,480,800};}
 void setClipRect(int,int,int,int){}FontCache*getFontCacheManager(){++fontReads;return &fonts;}
 int getScreenWidth(){return 480;}void displayBuffer(){}};
struct{char dictionaryName[2]{};int getReaderFontId(){assert(depth==1);return 2;}}SETTINGS;
struct DictionaryRegistry{static bool resolveBasePath(const char*,std::string&){return true;}};
struct Input{bool hasTouch(){return true;}struct Labels{const char*btn1="",*btn2="",*btn3="",*btn4="";};
 template<class...T>Labels mapLabels(T...){return{};}};
struct Theme{static Theme&getInstance(){static Theme t;return t;}Rect getScreenSafeArea(Renderer&,bool,bool){return{};}
 void drawButtonHints(Renderer&,const char*,const char*,const char*,const char*){}}GUI;
using UITheme=Theme;
struct Page{void render(Renderer&,int id,int,int){assert(depth==1&&id==2);++painted;}};
struct Popup{bool active=false;int paints=0;template<class...T>void show(T...){active=true;}
 bool processRender(Renderer&,Input&){if(active){++paints;return true;}return false;}};
struct ClipSelectionActivity:Activity{
 enum class Mode{Clip,DefinitionLookup};Mode mode=Mode::Clip;
 Renderer renderer;Input mappedInput;Popup actionPopup;std::atomic<bool>renderReady{false};
 int fontId=0,lineHeight=0,selected=0,rangeStart=-1,currentPageOffset=0,initialX=-1,initialY=-1;
 int marginLeft=0,marginTop=0,lastRenderedPageOffset=0,lastRenderedTextOffset=0,lastRenderedTextXOffset=0,lastRenderedRangeStart=0,lastRenderedSelected=0;
 bool lookupAvailable=false,ignoreInitialTouch=false,cancelled=false,extractOk=true,waitInExtract=false;
 struct WordBox{uint8_t pageOffset=0;uint16_t row=0,pageWordIndex=0;};std::unique_ptr<WordBox[]>words;size_t wordCount=0;
 struct{bool valid=false;int firstPage=0,lastPage=0,firstWord=0,lastWord=0;}initialSelection;
 bool extractWords(){assert(depth==1);if(waitInExtract){std::unique_lock<std::mutex>l(initMutex);extracting=true;changed.notify_all();changed.wait(l,[]{return finishInit;});}
  if(!extractOk)return false;words=std::make_unique<WordBox[]>(1);wordCount=1;return true;}
 int closestInRow(int,int){return 0;}int wordAt(int,int){return 0;}void cancel(){cancelled=true;}void requestUpdate(){}
 int textOffset(){return 0;}int textXOffset(){return 0;}bool renderIncremental(){return false;}
 size_t pageCount()const{return 1;}Page page;Page&pageAt(int index){assert(index==0);return page;}
 bool isLookup(){return mode!=Mode::Clip;}void drawSelection(){assert(words&&wordCount==1);}
 void onEnter();void render(RenderLock&&);
};
''' + enter + '\n' + render + r'''
void paint(ClipSelectionActivity&c){RenderLock lock;c.render(std::move(lock));}
int main(){
 ClipSelectionActivity fresh;paint(fresh);assert(painted==0&&fontReads==0);
 fresh.waitInExtract=true;
 std::thread init([&]{fresh.onEnter();});
 {std::unique_lock<std::mutex>l(initMutex);changed.wait(l,[]{return extracting;});}
 std::atomic<bool>attempted{false};std::thread queued([&]{attempted=true;paint(fresh);});
 while(!attempted.load())std::this_thread::yield();assert(painted==0);
 {std::lock_guard<std::mutex>l(initMutex);finishInit=true;}changed.notify_all();init.join();queued.join();
 assert(fresh.renderReady&&painted==2&&fontReads==1);
 ClipSelectionActivity failed;failed.extractOk=false;failed.onEnter();paint(failed);assert(failed.cancelled&&painted==2);
 ClipSelectionActivity lookup;lookup.mode=ClipSelectionActivity::Mode::DefinitionLookup;lookup.extractOk=false;
 lookup.onEnter();paint(lookup);assert(lookup.actionPopup.paints==1&&painted==2);
 lookup.actionPopup.active=false;paint(lookup);assert(painted==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(program)
 subprocess.run(['c++','-std=c++20','-pthread',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Queued clipping paints wait for initialized selection and keep error popups safe')
