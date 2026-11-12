"""Probe art availability without selecting an image or advancing its rotation."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'src/activities/boot_sleep/SleepActivity.cpp').read_text()
start=source.index('enum class PageOverlayArt {');end=source.index('\n#endif',start)
probe=source[start:end]
program=r'''
#include <cassert>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <utility>
#define LOG_ERR(...) ((void)0)
constexpr size_t MAX_SLEEP_FILE_NAME_LEN=256;
int liveFiles=0,scans=0,allocations=0;bool allocationAllowed=true;
struct File{
 std::string path;bool directory=false;
 File()=default;File(std::string p,bool d):path(std::move(p)),directory(d){++liveFiles;}
 File(const File&)=delete;File(File&&other):path(std::move(other.path)),directory(other.directory){other.path.clear();}
 explicit operator bool()const{return !path.empty();}bool isDirectory()const{return directory;}
 void close(){if(!path.empty()){--liveFiles;path.clear();}}~File(){close();}
};
using HalFile=File;
struct{std::set<std::string>files,dirs;bool exists(const char*p){return files.count(p);}
 File open(const char*p){if(dirs.count(p))return File(p,true);if(files.count(p))return File(p,false);return{};}}Storage;
struct{std::string favoriteSleepImagePath,preferredSleepFolderPath;}APP_STATE;
struct FsHelpers{static bool hasBmpExtension(const std::string&s){return s.ends_with(".bmp");}static bool hasPngExtension(const std::string&s){return s.ends_with(".png");}};
enum class SleepRecentKind{Overlay};std::set<std::string>validDirs;
bool findNextValidSleepImage(File&dir,SleepRecentKind,char*name){++scans;assert(name);return validDirs.count(dir.path);}
template<class T>std::unique_ptr<std::remove_extent_t<T>[]>makeUniqueNoThrow(size_t count){assert(count==256);++allocations;return allocationAllowed?std::make_unique<std::remove_extent_t<T>[]>(count):nullptr;}
''' + probe + r'''
int main(){
 auto check=[](PageOverlayArt expected){assert(findPageOverlayArt()==expected);assert(liveFiles==0);};
 check(PageOverlayArt::None);
 Storage.files.insert("/sleep.png");check(PageOverlayArt::Available);Storage.files.clear();
 Storage.files.insert("/favorite.bmp");APP_STATE.favoriteSleepImagePath="/favorite.bmp";
 const int before=allocations;allocationAllowed=false;check(PageOverlayArt::Available);assert(allocations==before);
 APP_STATE.favoriteSleepImagePath="/missing.png";check(PageOverlayArt::Unavailable);allocationAllowed=true;
 APP_STATE.favoriteSleepImagePath="/unsupported.gif";Storage.files.insert("/unsupported.gif");check(PageOverlayArt::None);
 Storage.dirs={"/.sleep","/sleep","/preferred"};validDirs={"/.sleep"};APP_STATE.preferredSleepFolderPath="/preferred";
 check(PageOverlayArt::None); // An existing preferred directory suppresses the default folders.
 validDirs.insert("/preferred");check(PageOverlayArt::Available);
 validDirs.erase("/preferred");APP_STATE.preferredSleepFolderPath="/missing";check(PageOverlayArt::Available);
 validDirs.clear();Storage.files.insert("/sleep.bmp");check(PageOverlayArt::Available);
 Storage.files.clear();APP_STATE.preferredSleepFolderPath="/preferred";check(PageOverlayArt::None);
 assert(scans>0);
}
'''
with tempfile.TemporaryDirectory() as directory:
 path=Path(directory);(path/'test.cpp').write_text(program)
 subprocess.run(['c++','-std=c++20',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
 subprocess.run([str(path/'test')],check=True)
print('Art probe preserves source priority and releases bounded storage before reader rendering')
