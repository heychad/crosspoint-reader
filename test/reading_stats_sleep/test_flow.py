"""Exercise production sleep-screen guards and fallback with controllable failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
start = source.index('void SleepActivity::renderReadingStatsSleepScreen() const {')
end = source.index('\nbool SleepActivity::renderCustomSleepBmp', start)
method = source[start:end]
assert 'case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_SUMMARY_SLEEP):' in source
assert 'case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_SUMMARY_SLEEP):' in source
assert 'values[CrossPointSettings::READING_SUMMARY_SLEEP] = StrId::STR_READING_SUMMARY;' in (root / 'src/SettingsList.cpp').read_text()
assert 'values[CrossPointSettings::COVER_SUMMARY_SLEEP] = StrId::STR_COVER_READING_SUMMARY;' in (root / 'src/SettingsList.cpp').read_text()
code = r'''
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <type_traits>
bool bitmapAllocation=true,coverDrawResult=true;
bool thumb300=false,thumb226=false,closeOk=true,headersOk=true; int bmpWidth=180,bmpHeight=300,bmpBpp=1,bmpRow=24,coverOpens=0;
struct HalFile {bool close(){return closeOk;}};
enum class BmpReaderError {Ok,Bad};
struct Bitmap {explicit Bitmap(HalFile&){} BmpReaderError parseHeaders(){return headersOk?BmpReaderError::Ok:BmpReaderError::Bad;} int getWidth(){return bmpWidth;} int getHeight(){return bmpHeight;} int getBpp(){return bmpBpp;} int getRowBytes(){return bmpRow;}};
struct UITheme {static std::string getCoverThumbPath(const std::string& path,int h){return path+std::to_string(h);}};
struct DateTime{}; struct BookReadingStats {enum class LoadMode {ReadOnly};bool writable=true;};
struct GlobalReadingStats {bool writable=true; static GlobalReadingStats loadReadOnly(){return {};}};
namespace sleepstats {
 struct Data {BookReadingStats book; GlobalReadingStats device; DateTime now; bool hasClock=false,hasDevice=false;int progress=-1;};
 bool drawResult=true; std::string title; int progress=0;bool usedSummary=false,usedCover=false;
 template<class Renderer> bool draw(Renderer&,const char* text,const Data& data){usedCover=false;usedSummary=false;title=text;progress=data.progress;return drawResult;}
 template<class Renderer> bool drawSummary(Renderer&,const char* text,const Data& data){usedCover=false;usedSummary=true;title=text;progress=data.progress;return drawResult;}
 template<class Renderer> bool drawCoverSummary(Renderer&,const char* text,const Data& data,const Bitmap&){usedCover=true;usedSummary=false;title=text;progress=data.progress;return coverDrawResult;}
}
struct {bool trackReadingStats=true;int sleepScreenCoverFilter=0,sleepScreen=0;} SETTINGS;
struct {std::string openEpubPath="/books/example.epub";} APP_STATE;
struct CrossPointSettings {enum SLEEP_SCREEN_COVER_FILTER {INVERTED_BLACK_AND_WHITE=2};enum {READING_SUMMARY_SLEEP=10,COVER_SUMMARY_SLEEP=11};};
struct RecentBook {std::string path,title,coverBmpPath;};
struct {std::vector<RecentBook> books;const auto& getBooks()const{return books;}} RECENT_BOOKS;
struct Renderer {int inverted=0,displayed=0;void invertScreen(){++inverted;}void displayBuffer(int){++displayed;}} renderer;
constexpr int kSleepClean=1;
bool allocation=true,enabled=true,found=true,writable=true,clockValid=false,bookType=true;int reads=0,progressReads=0;
struct HalStorage {enum class PathStatus {Present, Missing, Failed};};
struct {HalStorage::PathStatus result=HalStorage::PathStatus::Present;HalStorage::PathStatus lookupPath(const char*){return result;} bool openFileForRead(const char*,const std::string& p,HalFile&){++coverOpens;return (p.ends_with("300") && thumb300) || (p.ends_with("226") && thumb226);}} Storage;
namespace FsHelpers {bool hasReflowableBookExtension(const std::string&){return bookType;}bool hasXtcExtension(const std::string&){return false;}bool hasEpubExtension(const std::string& p){return p.ends_with(".epub");}}
struct Epub {static std::string cachePathForFilePath(const std::string&,const std::string&){return "cache";}};
struct Xtc {Xtc(const std::string&,const std::string&){}std::string getCachePath()const{return "cache";}};
template<class T,class... Args>std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){if(!allocation)return {};if constexpr(std::is_same_v<T,Bitmap>){if(!bitmapAllocation)return {};}return std::make_unique<T>(std::forward<Args>(args)...);}
namespace readingstats {BookReadingStats loadBook(const std::string&,const std::string&,bool& on,bool* exists,BookReadingStats::LoadMode){++reads;on=enabled;*exists=found;return {writable};}}
bool getCurrentLocalReadingStatsDateTime(DateTime&){return clockValid;}
int loadBookProgress(const std::string& path){++progressReads;return path.ends_with(".txt")?99:42;}
struct SleepActivity {mutable int defaults=0;void renderDefaultSleepScreen()const{++defaults;}void renderReadingStatsSleepScreen()const;};
''' + method + r'''
int main(){
 SleepActivity a;
 SETTINGS.trackReadingStats=false;a.renderReadingStatsSleepScreen();if(a.defaults!=1||reads) return 1;
 SETTINGS.trackReadingStats=true;Storage.result=HalStorage::PathStatus::Failed;a.renderReadingStatsSleepScreen();if(a.defaults!=2||reads) return 2;
 Storage.result=HalStorage::PathStatus::Present;bookType=false;a.renderReadingStatsSleepScreen();if(a.defaults!=3||reads) return 3;
 bookType=true;allocation=false;a.renderReadingStatsSleepScreen();if(a.defaults!=4||reads) return 4;
 allocation=true;enabled=false;a.renderReadingStatsSleepScreen();if(a.defaults!=5||renderer.displayed) return 5;
 enabled=true;found=false;a.renderReadingStatsSleepScreen();if(a.defaults!=6||renderer.displayed) return 6;
 found=true;writable=false;a.renderReadingStatsSleepScreen();if(a.defaults!=7||renderer.displayed) return 7;
 writable=true;sleepstats::drawResult=false;a.renderReadingStatsSleepScreen();if(a.defaults!=8||renderer.displayed) return 8;
 sleepstats::drawResult=true;RECENT_BOOKS.books.push_back({APP_STATE.openEpubPath,"Correct title",""});
 a.renderReadingStatsSleepScreen();if(renderer.displayed!=1||renderer.inverted!=0||sleepstats::title!="Correct title"||sleepstats::progress!=42) return 9;
 SETTINGS.sleepScreenCoverFilter=2;clockValid=true;APP_STATE.openEpubPath="/another/unknown.epub";
 a.renderReadingStatsSleepScreen();if(renderer.displayed!=2||renderer.inverted!=1||sleepstats::title!="unknown.epub") return 10;
 APP_STATE.openEpubPath="/books/stale.txt";const int before=progressReads;
 a.renderReadingStatsSleepScreen();if(progressReads!=before||sleepstats::progress!=-1||renderer.displayed!=3) return 11;
 SETTINGS.sleepScreen=CrossPointSettings::READING_SUMMARY_SLEEP;APP_STATE.openEpubPath="/books/example.epub";
 a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=4||sleepstats::progress!=42) return 12;
 sleepstats::drawResult=false;a.renderReadingStatsSleepScreen();if(a.defaults!=9||renderer.displayed!=4) return 13;
 sleepstats::drawResult=true;SETTINGS.sleepScreen=CrossPointSettings::COVER_SUMMARY_SLEEP;RECENT_BOOKS.books[0].coverBmpPath="thumb";
 thumb300=true;a.renderReadingStatsSleepScreen();if(!sleepstats::usedCover||renderer.displayed!=5) return 14;
 thumb300=false;thumb226=true;a.renderReadingStatsSleepScreen();if(!sleepstats::usedCover||renderer.displayed!=6) return 15;
 thumb226=false;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=7) return 16;
 thumb300=true;bmpWidth=2048;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=8) return 17;
 bmpWidth=180;closeOk=false;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=9) return 18;
 closeOk=true;headersOk=false;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=10) return 19;
 headersOk=true;bmpBpp=24;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=11) return 20;
 bmpBpp=1;bmpHeight=513;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=12) return 21;
 bmpHeight=300;bmpRow=257;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=13) return 22;
 bmpRow=24;bitmapAllocation=false;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=14) return 23;
 bitmapAllocation=true;coverDrawResult=false;a.renderReadingStatsSleepScreen();if(!sleepstats::usedSummary||renderer.displayed!=15) return 24;



 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    target = Path(directory)
    (target / 'flow.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(target / 'flow.cpp'), '-o', str(target / 'flow')], check=True)
    subprocess.run([str(target / 'flow')], check=True)
print('Statistics sleep fallback and saved-book flow passed')
