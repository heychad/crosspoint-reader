"""Exercise page-overlay selection and fallback without device hardware."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
start = source.index('void SleepActivity::renderPageOverlaySleepScreen() const {')
end = source.index('\n#endif', start)
method = source[start:end]
program = r'''
#include <cassert>
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
struct HalDisplay {};
constexpr int kSleepClean=1;
struct Renderer { int displays=0; void displayBuffer(int){displays++;} };
struct SleepActivity {
 mutable Renderer renderer;
 mutable int defaults=0, calls=0;
 mutable std::string last;
 std::set<std::string> failures;
 bool renderSleepOverlayPath(const std::string& p) const {calls++;last=p;return !failures.count(p);}
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
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Page overlay selection and fallback passed')
