"""Exercise production sleep preparation, stack ownership, and EPUB buffer-only drawing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
def body(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)

sleep = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
epub = (root / 'src/activities/reader/EpubReaderActivity.cpp').read_text()
manager = (root / 'src/activities/ActivityManager.cpp').read_text()
prepare = body(sleep, 'void SleepActivity::prepareReaderPageBackground(')
start = sleep.index('void SleepActivity::onEnter()')
preamble = sleep[start:sleep.index('  const bool frameWasInverted', start)] + '  ++otherPaints;\n}'
restore = body(epub, 'uint32_t EpubReaderActivity::sleepPageLayoutSignature(') + '\n' + body(epub, 'bool EpubReaderActivity::restorePageForSleep(')
replace = body(manager, 'if (pendingAction == PendingAction::Replace || pendingAction == PendingAction::ReplaceForSleep) {')
program = r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <cstring>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY 1
struct CrossPointSettings{enum SLEEP_SCREEN_MODE{QUICK_RESUME=7,PAGE_OVERLAY_SLEEP=14};enum QUICK_RESUME_SLEEP_SCREEN{QUICK_RESUME_AFTER_TIMEOUT=1};};
struct{int sleepScreen=14,quickResumeSleepScreen=0,orientation=0;uint8_t paragraphIndentSpaces=2,fontPointSize=14,fontFamily=0;char sdFontFamilyName[32]{};int getReaderFontId()const{return 2;}}SETTINGS;
int locks=0,draws=0,scans=0,imagesReleased=0,settles=0,discards=0;
struct RenderLock {RenderLock(){++locks;}~RenderLock(){--locks;}};
struct Fonts{
 bool scanning=false;
 struct Scope{Fonts&f;void endScanAndPrewarm(){assert(f.scanning);f.scanning=false;++scans;}};
 Scope createPrewarmScope(){scanning=true;return{*this};}
};
struct GfxRenderer{
 enum Orientation{Portrait,Landscape,Inverted,LandscapeCCW};enum RenderMode{BW,GRAYSCALE_LSB,GRAYSCALE_MSB};
 Orientation orientation=Landscape;RenderMode mode=GRAYSCALE_MSB;
 std::array<int,4>clip={7,8,9,10};bool buffer=true,dither=false,hasFonts=true;Fonts fonts;
 int cleared=0;
 Orientation getOrientation()const{return orientation;}void setOrientation(Orientation o){orientation=o;}
 int getScreenWidth()const{return orientation==Portrait||orientation==Inverted?480:800;}
 int getScreenHeight()const{return orientation==Portrait||orientation==Inverted?800:480;}
 auto getClipRect()const{return clip;}void setClipRect(int x,int y,int w,int h){clip={x,y,w,h};}
 RenderMode getRenderMode()const{return mode;}void setRenderMode(RenderMode m){mode=m;}
 bool hasFrameBuffer()const{return buffer;}Fonts*getFontCacheManager(){return hasFonts?&fonts:nullptr;}
 bool imagesDitheredInBw()const{return dither;}void setDitherImagesInBw(bool b){dither=b;}
 void clearScreen(){++cleared;}
};
namespace ReaderUtils{void applyOrientation(GfxRenderer&r,int o){r.setOrientation(static_cast<GfxRenderer::Orientation>(o));}}
struct ImageBlock{static void clearRenderFailures(){}static void releaseRenderCache(){++imagesReleased;}};
struct Page{void render(GfxRenderer&r,int font,int x,int y){assert(font==2&&x==7&&y==33);assert(r.mode==GfxRenderer::BW);++draws;}};
struct Section{bool building=false,readable=true;int currentPage=1,pageCount=3,loads=0;
 bool isBuilding()const{return building;}std::unique_ptr<Page>loadPage(int page){assert(page==currentPage);++loads;return readable?std::make_unique<Page>():nullptr;}};
struct ReaderActivity{virtual~ReaderActivity()=default;enum class SleepPageTarget{Buffer,Display};virtual bool restorePageForSleep(SleepPageTarget)=0;};
uint32_t readerRenderSpecSignature(int value){return 100+value;}
struct EpubReaderActivity:ReaderActivity{
 GfxRenderer&renderer;bool epub=true;std::unique_ptr<Section>section=std::make_unique<Section>();
 int buildViewportWidth=466,buildViewportHeight=734,status=0,currentSpineIndex=0,specVersion=0,fullPaints=0;
 uint32_t renderedSleepLayout=0;uint32_t sleepPageLayoutSignature()const;
 int effectiveRenderSpec(int,int,int)const{return specVersion;}
 struct Layout{int left,top,right,bottom,width,height;};
 Layout readerPageLayout()const{return{7,33,7,33,renderer.getScreenWidth()-14,renderer.getScreenHeight()-66};}
 void settleOverlayRefresh(){++settles;}void discardOverlayPage(){++discards;}
 void renderStatusBar(){++status;}void drawClippingHighlights(Page&,int,int top,int left){assert(top==33&&left==7);}
 void renderContents(std::unique_ptr<Page>p,int t,int r,int b,int l){assert(p&&t==33&&r==7&&b==33&&l==7);++fullPaints;}
 explicit EpubReaderActivity(GfxRenderer&r):renderer(r){renderedSleepLayout=sleepPageLayoutSignature();}bool restorePageForSleep(SleepPageTarget target)override;
};
enum class PageOverlayArt{None,Available,Unavailable};PageOverlayArt art=PageOverlayArt::Available;
PageOverlayArt findPageOverlayArt(){return art;}
struct Activity{void onEnter(){}};
struct{bool inverted=true;void setInverted(bool b){inverted=b;}}display;
struct SleepActivity:Activity{
 GfxRenderer&renderer;bool fromTimeout=false;
 enum class PageOverlayBackground{RetainedScreen,PreparedPage,DisplayedPage,Unavailable};
 PageOverlayBackground pageOverlayBackground=PageOverlayBackground::RetainedScreen;
 bool backgroundReady()const{return pageOverlayBackground!=PageOverlayBackground::Unavailable;}
 int quickPaints=0,defaults=0,otherPaints=0;
 void renderLastScreenSleepScreen(){++quickPaints;}void renderDefaultSleepScreen(){++defaults;}
 void onEnter();
 explicit SleepActivity(GfxRenderer&r):renderer(r){}void prepareReaderPageBackground(ReaderActivity*);
};
''' + prepare + '\n' + restore + '\n' + preamble + r'''
int main(){
 for(int orientation=0;orientation<4;++orientation){
  GfxRenderer r;EpubReaderActivity reader(r);SETTINGS.orientation=orientation;
  reader.buildViewportWidth=(orientation%2?800:480)-14;
  reader.buildViewportHeight=(orientation%2?480:800)-66;
  SleepActivity sleep(r);const auto clip=r.clip;const auto mode=r.mode;const auto direction=r.orientation;
  sleep.prepareReaderPageBackground(&reader);
  assert(sleep.backgroundReady()&&reader.section->currentPage==1&&reader.section->loads==1);
  assert(reader.status==2&&r.clip==clip&&r.mode==mode&&r.orientation==direction&&!r.dither);
 }
 GfxRenderer r;EpubReaderActivity reader(r);SETTINGS.orientation=0;SleepActivity sleep(r);
 auto savedSignature=reader.renderedSleepLayout;reader.renderedSleepLayout=0;
 sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady()&&reader.section->loads==0);
 reader.renderedSleepLayout=savedSignature;
 SETTINGS.fontPointSize=18;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady()&&reader.section->loads==0);SETTINGS.fontPointSize=14;
 SETTINGS.fontFamily=1;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());SETTINGS.fontFamily=0;
 reader.section->building=true;sleep.prepareReaderPageBackground(&reader);assert(sleep.backgroundReady()&&reader.section->loads==1);
 reader.specVersion=1;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady()&&reader.section->loads==1);reader.specVersion=0;
 SETTINGS.paragraphIndentSpaces=3;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());SETTINGS.paragraphIndentSpaces=2;
 strcpy(SETTINGS.sdFontFamilyName,"Other");sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());SETTINGS.sdFontFamilyName[0]=0;
 reader.section->building=false;reader.section->readable=false;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());
 reader.section->readable=true;r.hasFonts=false;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());r.hasFonts=true;
 reader.buildViewportWidth=1;int before=reader.section->loads;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady()&&reader.section->loads==before);
 reader.buildViewportWidth=466;r.buffer=false;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());r.buffer=true;
 for(int page:{-1,3,4}){reader.section->currentPage=page;sleep.prepareReaderPageBackground(&reader);assert(!sleep.backgroundReady());}
 reader.section->currentPage=1;
 before=reader.section->loads;SETTINGS.sleepScreen=0;sleep.prepareReaderPageBackground(&reader);assert(reader.section->loads==before);
 SETTINGS.sleepScreen=14;sleep.fromTimeout=true;SETTINGS.quickResumeSleepScreen=1;sleep.prepareReaderPageBackground(&reader);assert(reader.section->loads==before);
 SETTINGS.quickResumeSleepScreen=0;sleep.prepareReaderPageBackground(nullptr);assert(reader.section->loads==before);
 assert(imagesReleased==5&&settles==5&&discards==5&&draws==10&&scans==5);
 SleepActivity unavailable(r);unavailable.pageOverlayBackground=SleepActivity::PageOverlayBackground::Unavailable;SETTINGS.sleepScreen=14;
 unavailable.onEnter();assert(unavailable.defaults==1&&unavailable.otherPaints==0&&!display.inverted);
 unavailable.fromTimeout=true;SETTINGS.quickResumeSleepScreen=1;display.inverted=true;
 unavailable.onEnter();assert(unavailable.quickPaints==1&&unavailable.defaults==1&&display.inverted);
 SETTINGS.quickResumeSleepScreen=0;SETTINGS.sleepScreen=7;
 unavailable.onEnter();assert(unavailable.quickPaints==2&&unavailable.defaults==1);
 SETTINGS.sleepScreen=0;unavailable.onEnter();assert(unavailable.otherPaints==1);
 SETTINGS.sleepScreen=14;SleepActivity gray(r);art=PageOverlayArt::None;
 gray.prepareReaderPageBackground(&reader);assert(gray.pageOverlayBackground==SleepActivity::PageOverlayBackground::DisplayedPage&&reader.fullPaints==1);
 display.inverted=true;gray.onEnter();assert(gray.defaults==0&&gray.otherPaints==0&&display.inverted);
 art=PageOverlayArt::Unavailable;const int reads=reader.section->loads;
 gray.prepareReaderPageBackground(&reader);assert(!gray.backgroundReady()&&reader.section->loads==reads);


}
'''
stack = r'''
#include <cassert>
#include <memory>
#include <cstring>
#include <vector>
#define CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY 1
int lockDepth=0;
struct RenderLock{RenderLock(){++lockDepth;}~RenderLock(){--lockDepth;}};
struct ReaderActivity{int id;};
struct Activity{ReaderActivity*reader=nullptr;bool*live;Activity(bool&l,ReaderActivity*r=nullptr):reader(r),live(&l){}virtual~Activity()=default;
 ReaderActivity*bookReader(){return reader;}void onExit(){*live=false;}};
struct SleepActivity:Activity{ReaderActivity*selected=nullptr;bool parentWasLive=false;bool*parent;
 SleepActivity(bool&l,bool&p):Activity(l),parent(&p){}void prepareReaderPageBackground(ReaderActivity*r){assert(lockDepth==1);selected=r;parentWasLive=*parent;}};
struct ActivityManager{
 enum class PendingAction{Replace,ReplaceForSleep};PendingAction pendingAction=PendingAction::ReplaceForSleep;
 std::unique_ptr<Activity>currentActivity,pendingActivity;std::vector<std::unique_ptr<Activity>>stackActivities;
 void exitActivity(RenderLock&){if(currentActivity)currentActivity->onExit();currentActivity.reset();}
 void apply(){RenderLock lock;
''' + replace + r'''
 }
};
int main(){for(int foreground=0;foreground<2;++foreground){
 bool a=true,b=true,child=true,s=true;ReaderActivity old{1},recent{2};ActivityManager m;
 m.stackActivities.push_back(std::make_unique<Activity>(a,&old));m.stackActivities.push_back(std::make_unique<Activity>(b,&recent));
 m.currentActivity=std::make_unique<Activity>(child,foreground?&old:nullptr);m.pendingActivity=std::make_unique<SleepActivity>(s,b);
 auto*sleep=static_cast<SleepActivity*>(m.pendingActivity.get());m.apply();
 assert(sleep->selected==(foreground?&old:&recent)&&sleep->parentWasLive&&!a&&!b&&!child&&m.stackActivities.empty());
 }
 bool live=true,s=true;ActivityManager m;m.currentActivity=std::make_unique<Activity>(live);m.pendingActivity=std::make_unique<SleepActivity>(s,live);
 m.pendingAction=ActivityManager::PendingAction::Replace;m.apply();assert(static_cast<SleepActivity*>(m.pendingActivity.get())->selected==nullptr);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    for name, source in [('page', program), ('stack', stack)]:
        (path / (name + '.cpp')).write_text(source)
        subprocess.run(['c++', '-std=c++20', str(path / (name + '.cpp')), '-o', str(path / name)], check=True)
        subprocess.run([str(path / name)], check=True)
print('Reader sleep reconstruction preserves state, geometry, quick-resume priority, and activity lifetimes')
