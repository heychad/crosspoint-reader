"""Exercise page-overlay selection and fallback without device hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
start = source.index('void SleepActivity::renderPageOverlaySleepScreen() const {')
end = source.index('\n#endif', start)
method = source[start:end]
fallback_start = source.index('void SleepActivity::renderOverlayFallback() const {')
fallback_end = source.index('\n#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)', fallback_start)
method = source[fallback_start:fallback_end] + method
program = r'''
#include <cassert>
#include <cstdio>
#include <set>
#include <string>
struct File { bool present=false; explicit operator bool() const {return present;} bool isDirectory(){return present;} void close(){} };
using HalFile=File;
struct Store { std::set<std::string> files, directories; bool exists(const char* p){return files.count(p);} File open(const char* p){return {directories.count(p)>0};} } Storage;
struct State { std::string favoriteSleepImagePath, preferredSleepFolderPath; } APP_STATE;
struct FsHelpers { static bool hasBmpExtension(const std::string& p){return p.ends_with(".bmp");} static bool hasPngExtension(const std::string& p){return p.ends_with(".png");} };
enum class SleepRecentKind { Overlay };
std::string selected, firstDir, secondDir;
bool selectRandomSleepFile(const char* first,const char* second,SleepRecentKind,std::string& out){firstDir=first;secondDir=second?second:"";out=selected;return !out.empty();}
struct CrossPointSettings {enum {PAGE_OVERLAY_SLEEP=14,COVER_OVERLAY_SLEEP=15};};
struct {int sleepScreen=14;} SETTINGS;
struct HalDisplay {};
enum class BmpReaderError {Ok,Failed};
struct Bitmap {bool readable=true;BmpReaderError rewindToData()const{return readable?BmpReaderError::Ok:BmpReaderError::Failed;}};
constexpr int kSleepClean=1;
struct GfxRenderer { enum {BW};int displays=0,clears=0,redraws=0;bool dirty=false;
 void displayBuffer(int){displays++;}void clearScreen(){++clears;dirty=false;}};
using Renderer=GfxRenderer;
bool beginOverlayGrayPlane(Renderer&r,const Bitmap*cover,int,bool){++r.redraws;r.dirty=false;return cover->readable;}
struct SleepActivity {
 mutable Renderer renderer;
 mutable int defaults=0, calls=0,plainCovers=0;
 mutable const Bitmap*overlayCover=nullptr;
 bool failPlain=false;
 mutable std::string last;
 std::set<std::string> failures;
 bool renderSleepOverlayPath(const std::string& p) const {
   if(overlayCover)assert(!renderer.dirty);++calls;last=p;
   if(failures.count(p)){renderer.dirty=true;return false;}return true;
 }
 bool renderBitmapSleepScreen(const Bitmap&)const{assert(!overlayCover);++plainCovers;return !failPlain;}
 void renderOverlayFallback()const;
 void renderDefaultSleepScreen() const {defaults++;}
 void renderPageOverlaySleepScreen() const;
};
''' + method + r'''
int main() {
 SleepActivity a;
 a.renderPageOverlaySleepScreen(); assert(a.renderer.displays==1 && a.defaults==0);
 Storage.files.insert("/sleep.png"); a.renderPageOverlaySleepScreen(); assert(a.last=="/sleep.png");
 Storage.files.insert("/art/fav.png"); APP_STATE.favoriteSleepImagePath="/art/fav.png";
 a.renderPageOverlaySleepScreen(); assert(a.last=="/art/fav.png");
 a.failures.insert("/art/fav.png"); int calls=a.calls; a.renderPageOverlaySleepScreen();
 assert(a.defaults==1 && a.calls==calls+1);
 APP_STATE.favoriteSleepImagePath.clear(); APP_STATE.preferredSleepFolderPath="/myart";
 Storage.directories.insert("/myart"); Storage.files.insert("/myart/one.bmp"); selected="/myart/one.bmp";
 a.renderPageOverlaySleepScreen(); assert(firstDir=="/myart" && secondDir.empty() && a.last==selected);
 Storage.directories.clear(); selected.clear(); a.renderPageOverlaySleepScreen();
 assert(firstDir=="/.sleep" && secondDir=="/sleep" && a.last=="/sleep.png");
 SETTINGS.sleepScreen=15;Bitmap cover;a.overlayCover=&cover;
 Storage.files.clear();selected.clear();a.failures.clear();
 a.renderPageOverlaySleepScreen();assert(a.plainCovers==1&&!a.overlayCover);
 a.overlayCover=&cover;APP_STATE.favoriteSleepImagePath="/art/bad.png";
 Storage.files.insert("/art/bad.png");Storage.files.insert("/sleep/good.bmp");selected="/sleep/good.bmp";
 a.failures.insert("/art/bad.png");int redraws=a.renderer.redraws;
 a.renderPageOverlaySleepScreen();assert(a.last==selected&&a.renderer.redraws==redraws+2&&a.overlayCover==&cover);
 a.failures.insert(selected);a.renderPageOverlaySleepScreen();assert(a.plainCovers==2&&!a.overlayCover);
 a.overlayCover=&cover;a.failPlain=true;a.renderPageOverlaySleepScreen();assert(a.defaults==2);
 a.failPlain=false;cover.readable=false;a.overlayCover=&cover;a.renderPageOverlaySleepScreen();assert(a.defaults==3);
 cover.readable=true;a.overlayCover=nullptr;a.failures.clear();int cleared=a.renderer.clears;
 a.renderPageOverlaySleepScreen();assert(a.renderer.clears==cleared+1&&a.last=="/art/bad.png");
 Storage.files.clear();APP_STATE.favoriteSleepImagePath.clear();selected.clear();
 const int defaultsBefore=a.defaults;a.renderPageOverlaySleepScreen();
 if(a.defaults!=defaultsBefore+1){std::fprintf(stderr,"Missing cover and artwork left a blank sleep screen\n");return 2;}
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Page overlay selection and fallback passed')
