"""Exercise real sleep-mode labels, entry routing, and cover ownership."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
header = (root / 'src/CrossPointSettings.h').read_text()
enum_start = header.index('  enum SLEEP_SCREEN_MODE {')
enum_text = header[enum_start:header.index('  };', enum_start) + 4]
labels_source = (root / 'src/SettingsList.cpp').read_text()
labels_start = labels_source.index('  static constexpr auto SLEEP_SCREEN_VALUES = [] {')
labels = labels_source[labels_start:labels_source.index('  }();', labels_start) + 7]


def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)


program = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <string>
#include <I18nKeys.h>
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
struct CrossPointSettings {
''' + enum_text + r'''
 enum SLEEP_SCREEN_COVER_MODE {FIT,CROP};
 enum QUICK_RESUME_SLEEP_SCREEN {QUICK_RESUME_AFTER_TIMEOUT=1};
};
''' + labels + r'''
struct {int sleepScreen=0,quickResumeSleepScreen=0,orientation=0,sleepScreenCoverMode=0;} SETTINGS;
struct {bool lastSleepFromReader=false;std::string openEpubPath="/book.epub";} APP_STATE;
struct GfxRenderer {
 enum class Orientation {Portrait,Landscape,Inverted,LandscapeCCW};
 Orientation orientation=Orientation::Portrait;
 void setOrientation(Orientation value){orientation=value;}
 void invertScreen(){}
};
struct {bool inverted=true;bool isInverted()const{return inverted;}void setInverted(bool value){inverted=value;}} display;
struct {void drawPopup(GfxRenderer&,const char*){}} GUI;
constexpr auto STR_ENTERING_SLEEP=StrId::STR_ENTERING_SLEEP;
const char*tr(StrId){return "sleep";}
namespace ReaderUtils{void applyOrientation(GfxRenderer&r,int value){r.setOrientation(static_cast<GfxRenderer::Orientation>(value));}}
int preserved=0;
bool drawSleepPopupPreservingFrame(GfxRenderer&){++preserved;return false;}
void releaseSdFontCachesForDecode(GfxRenderer&){}
bool useOriginalCoverThresholds(const GfxRenderer&){return false;}
bool documentLoad=true,generate=true,cached=true,validBitmap=true,drawSuccess=true;
struct HalFile{};
struct {bool openFileForRead(const char*,const std::string&,HalFile&){return cached;}} Storage;
enum class BmpReaderError {Ok,Failed};
struct Bitmap {Bitmap(HalFile&){}BmpReaderError parseHeaders()const{return validBitmap?BmpReaderError::Ok:BmpReaderError::Failed;}};
namespace FsHelpers {
 bool hasXtcExtension(const std::string&p){return p.ends_with(".xtc")||p.ends_with(".xtch");}
 bool hasReflowableBookExtension(const std::string&p){return p.ends_with(".epub")||p.ends_with(".txt")||p.ends_with(".md");}
}
struct Xtc {
 Xtc(const std::string&,const char*){}
 bool load(){return documentLoad;}bool generateCoverBmp(){return generate;}
 std::string getCoverBmpPath()const{return "/cover.bmp";}
};
struct Epub {
 Epub(const std::string&,const char*){}
 bool load(bool,bool){return documentLoad;}
 bool generateCoverBmp(bool,bool){return generate;}
 std::string getCoverBmpPath(bool,bool)const{return "/cover.bmp";}
};
struct Activity {void onEnter(){}};
struct SleepActivity:Activity {
 mutable GfxRenderer renderer;
 bool fromTimeout=false;
 mutable const Bitmap*overlayCover=nullptr;
 enum class PageOverlayBackground {RetainedScreen,PreparedPage,DisplayedPage,Unavailable};
 PageOverlayBackground pageOverlayBackground=PageOverlayBackground::Unavailable;
 mutable int defaults=0,quick=0,transparent=0,pages=0,custom=0,blanks=0,minimal=0,stats=0,bitmaps=0;
 mutable bool sawCoverOverlay=false;
 void renderDefaultSleepScreen()const{++defaults;}
 void renderLastScreenSleepScreen()const{++quick;}
 void renderTransparentCustomSleepScreen()const{++transparent;}
 void renderPageOverlaySleepScreen()const{++pages;}
 void renderCustomSleepScreen()const{++custom;}
 void renderBlankSleepScreen()const{++blanks;}
 void renderMinimalBookSleepScreen()const{++minimal;}
 void renderReadingStatsSleepScreen()const{++stats;}
 bool renderBitmapSleepScreen(const Bitmap&)const{++bitmaps;sawCoverOverlay=overlayCover!=nullptr;return drawSuccess;}
 void onEnter();void renderCoverSleepScreen()const;
};
''' + method('void SleepActivity::onEnter()') + '\n' + method(
    'void SleepActivity::renderCoverSleepScreen() const') + r'''
void reset(){documentLoad=generate=cached=validBitmap=drawSuccess=true;APP_STATE={};APP_STATE.openEpubPath="/book.epub";SETTINGS={};preserved=0;}
int main(){
 assert(CrossPointSettings::COVER_TRANSPARENT==8&&CrossPointSettings::DASHBOARD_BOOK_SLEEP==13);
 assert(SLEEP_SCREEN_VALUES[8]==StrId::STR_COVER_TRANSPARENT);
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
 static_assert(CrossPointSettings::COVER_OVERLAY_SLEEP==15&&CrossPointSettings::SLEEP_SCREEN_MODE_COUNT==16);
 assert(SLEEP_SCREEN_VALUES[15]==StrId::STR_COVER_OVERLAY_SLEEP);
 for(int orientation=0;orientation<4;++orientation)for(bool fromReader:{false,true}){
   reset();SETTINGS.sleepScreen=15;SETTINGS.orientation=orientation;APP_STATE.lastSleepFromReader=fromReader;
   SleepActivity sleep;sleep.renderer.orientation=static_cast<GfxRenderer::Orientation>(orientation);sleep.onEnter();
   assert(sleep.bitmaps==1&&sleep.sawCoverOverlay&&!sleep.overlayCover&&sleep.defaults==0);
   assert(sleep.renderer.orientation==GfxRenderer::Orientation::Portrait&&preserved==0);
 }
 for(int failure=0;failure<6;++failure){
   reset();SETTINGS.sleepScreen=15;
   if(failure==0)APP_STATE.openEpubPath.clear();
   if(failure==1)documentLoad=false;
   if(failure==2)generate=false;
   if(failure==3)cached=false;
   if(failure==4)validBitmap=false;
   if(failure==5)drawSuccess=false;
   SleepActivity sleep;sleep.onEnter();assert(sleep.pages==1&&sleep.defaults==0&&!sleep.overlayCover);
 }
 for(const char*path:{"/book.epub","/book.txt","/book.md","/book.xtc","/book.xtch"}){
   reset();SETTINGS.sleepScreen=15;APP_STATE.openEpubPath=path;SleepActivity sleep;sleep.onEnter();
   assert(sleep.bitmaps==1&&sleep.sawCoverOverlay);
 }
 reset();SETTINGS.sleepScreen=15;SETTINGS.quickResumeSleepScreen=1;
 SleepActivity quick;quick.fromTimeout=true;quick.onEnter();assert(quick.quick==1&&quick.bitmaps==0);
#else
 static_assert(CrossPointSettings::SLEEP_SCREEN_MODE_COUNT==14);
 reset();SETTINGS.sleepScreen=15;SleepActivity unknown;unknown.onEnter();assert(unknown.defaults==1&&unknown.bitmaps==0);
#endif
 reset();SETTINGS.sleepScreen=3;SleepActivity plain;plain.onEnter();assert(plain.bitmaps==1&&!plain.sawCoverOverlay);
 reset();SETTINGS.sleepScreen=8;SleepActivity legacy;legacy.onEnter();assert(legacy.bitmaps==1&&legacy.sawCoverOverlay);
 reset();SETTINGS.sleepScreen=8;cached=false;SleepActivity legacyMissing;legacyMissing.onEnter();assert(legacyMissing.transparent==1);
 reset();SETTINGS.sleepScreen=4;APP_STATE.lastSleepFromReader=true;cached=false;SleepActivity custom;custom.onEnter();assert(custom.custom==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    for flags in ([], ['-DCROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY=1']):
        subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', *flags,
                        '-fsanitize=address,undefined', '-I', str(root / 'lib/I18n'),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('Sleep mode IDs/labels, cover routing, orientation, quick resume, and fallback passed')
