"""Exercise production sleep-screen guards and fallback with controllable failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/activities/boot_sleep/SleepActivity.cpp').read_text()
start = source.index('void SleepActivity::renderMinimalBookSleepScreen() const {')
end = source.index('\nbool SleepActivity::renderCustomSleepBmp', start)
method = source[start:end]
assert 'case (CrossPointSettings::SLEEP_SCREEN_MODE::READING_SUMMARY_SLEEP):' in source
assert 'case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_SUMMARY_SLEEP):' in source
assert 'case (CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_BOOK_SLEEP):' in source
assert 'values[CrossPointSettings::READING_SUMMARY_SLEEP] = StrId::STR_READING_SUMMARY;' in (root / 'src/SettingsList.cpp').read_text()
assert 'values[CrossPointSettings::COVER_SUMMARY_SLEEP] = StrId::STR_COVER_READING_SUMMARY;' in (root / 'src/SettingsList.cpp').read_text()
code = r'''
#include <memory>
#include <algorithm>
#include <cstdint>
#include <cstring>
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
struct ReadingStatsDate {int year=0,month=0,day=0;bool isValid()const{return year&&month&&day;}};
ReadingStatsDate stubStart{2026,11,8},stubFinished{};bool stubCompleted=false;
struct DateTime{ReadingStatsDate date;};using ReadingStatsDateTime=DateTime;
int compareReadingStatsDate(const ReadingStatsDate& a,const ReadingStatsDate& b){return a.day-b.day;}
uint16_t readingSpanDaysElapsed(const ReadingStatsDate& a,const ReadingStatsDate& b){return static_cast<uint16_t>(b.day-a.day);}
struct BookReadingStats {enum class LoadMode {ReadOnly};bool writable=true;uint32_t totalReadingSeconds=99,totalPagesTurned=10;uint16_t sessionCount=2;bool isCompleted=false;ReadingStatsDate startDate{2026,11,8},finishedDate{};uint32_t remainingReadingSeconds(int)const{return 50;}};
struct GlobalStatsAggregate;
struct GlobalReadingStats {static GlobalStatsAggregate loadAggregated(const GlobalReadingStats&);bool writable=true;uint32_t totalReadingSeconds=200,completedBooks=3;uint16_t currentReadingStreak(const ReadingStatsDate*)const{return 5;}static GlobalReadingStats loadReadOnly(){return {};}};
struct GlobalStatsAggregate {GlobalReadingStats stats;uint32_t sources=0;bool complete=true;};
uint32_t aggregateSources=1;bool aggregateComplete=true;int aggregateCalls=0;
GlobalStatsAggregate GlobalReadingStats::loadAggregated(const GlobalReadingStats& stats){++aggregateCalls;return {stats,aggregateSources,aggregateComplete};}
namespace sleepstats {
 struct Data {BookReadingStats book; GlobalReadingStats device;GlobalStatsAggregate allDevices; DateTime now; bool hasClock=false,hasDevice=false,hasAllDevices=false;int progress=-1;};
 struct DashboardData {int progress=-1;uint32_t readingSeconds=0,remainingSeconds=0,sessions=0,pages=0,globalSeconds=0,completedBooks=0,dailyAverageSeconds=0;ReadingStatsDate startDate;uint16_t currentStreak=0;bool hasStats=false,hasGlobal=false,hasDailyAverage=false,hasStreak=false;};
 bool sawAllDevices=false; bool drawResult=true; std::string title; int progress=0;bool usedSummary=false,usedCover=false;
 bool usedMinimal=false;uint32_t seconds=0;
 template<class Renderer> bool draw(Renderer&,const char* text,const Data& data){sawAllDevices=data.hasAllDevices;usedCover=false;usedSummary=false;title=text;progress=data.progress;return drawResult;}
 template<class Renderer> bool drawSummary(Renderer&,const char* text,const Data& data){usedCover=false;usedSummary=true;title=text;progress=data.progress;return drawResult;}
 template<class Renderer> bool drawCoverSummary(Renderer&,const char* text,const Data& data,const Bitmap&){usedCover=true;usedSummary=false;title=text;progress=data.progress;return coverDrawResult;}
 template<class Renderer> bool drawMinimalBook(Renderer&,const char* text,int value,uint32_t time,const Bitmap* cover){usedMinimal=true;usedCover=cover!=nullptr;title=text;progress=value;seconds=time;return drawResult;}
 bool usedDashboard=false;DashboardData dashboard;std::string subtitle;
 template<class Renderer> bool drawBookDashboard(Renderer&,const char* text,const char* detail,const DashboardData& data,const Bitmap* cover){usedDashboard=true;usedCover=cover!=nullptr;title=text;subtitle=detail;dashboard=data;return drawResult;}
}
struct {bool trackReadingStats=true;int sleepScreenCoverFilter=0,sleepScreen=0;} SETTINGS;
struct {std::string openEpubPath="/books/example.epub";} APP_STATE;
struct CrossPointSettings {enum SLEEP_SCREEN_COVER_FILTER {INVERTED_BLACK_AND_WHITE=2};enum {READING_STATS_SLEEP=9,READING_SUMMARY_SLEEP=10,COVER_SUMMARY_SLEEP=11,DASHBOARD_BOOK_SLEEP=13};};
struct RecentBook {std::string path,title,coverBmpPath,author;};
struct {std::vector<RecentBook> books;const auto& getBooks()const{return books;}} RECENT_BOOKS;
struct Renderer {int inverted=0,displayed=0;void invertScreen(){++inverted;}void displayBuffer(int){++displayed;}} renderer;
constexpr int kSleepClean=1;
bool allocation=true,enabled=true,found=true,writable=true,clockValid=false,bookType=true,globalMissing=false;int reads=0,progressReads=0;
struct HalStorage {enum class PathStatus {Present, Missing, Failed};};
struct {HalStorage::PathStatus result=HalStorage::PathStatus::Present;HalStorage::PathStatus lookupPath(const char* p){return globalMissing && std::strstr(p,"global_stats.bin") ? HalStorage::PathStatus::Missing : result;} bool openFileForRead(const char*,const std::string& p,HalFile&){++coverOpens;return (p.ends_with("300") && thumb300) || (p.ends_with("226") && thumb226);}} Storage;
namespace FsHelpers {bool hasReflowableBookExtension(const std::string&){return bookType;}bool hasXtcExtension(const std::string&){return false;}bool hasEpubExtension(const std::string& p){return p.ends_with(".epub");}}
struct Epub {static std::string cachePathForFilePath(const std::string&,const std::string&){return "cache";}};
struct Xtc {Xtc(const std::string&,const std::string&){}std::string getCachePath()const{return "cache";}};
template<class T,class... Args>std::unique_ptr<T> makeUniqueNoThrow(Args&&... args){if(!allocation)return {};if constexpr(std::is_same_v<T,Bitmap>){if(!bitmapAllocation)return {};}return std::make_unique<T>(std::forward<Args>(args)...);}
namespace readingstats {BookReadingStats loadBook(const std::string&,const std::string&,bool& on,bool* exists,BookReadingStats::LoadMode){++reads;on=enabled;*exists=found;BookReadingStats result;result.writable=writable;result.startDate=stubStart;result.finishedDate=stubFinished;result.isCompleted=stubCompleted;return result;}}
bool getCurrentLocalReadingStatsDateTime(DateTime& now){now.date={2026,11,10};return clockValid;}
int loadBookProgress(const std::string& path){++progressReads;return path.ends_with(".txt")?99:42;}
int loadBookProgress(const std::string& path,char* chapter,size_t capacity){if(capacity>1){chapter[0]='C';chapter[1]=0;}return loadBookProgress(path);}
struct SleepActivity {mutable int defaults=0;void renderDefaultSleepScreen()const{++defaults;}void renderReadingStatsSleepScreen()const;void renderMinimalBookSleepScreen()const;};
''' + method + r'''
int main(){
 SleepActivity a;
 aggregateSources=2;a.renderReadingStatsSleepScreen();if(!sleepstats::sawAllDevices||aggregateCalls!=1)return 101;
 aggregateSources=1;aggregateComplete=false;a.renderReadingStatsSleepScreen();if(!sleepstats::sawAllDevices)return 102;
 aggregateComplete=true;aggregateCalls=0;reads=0;progressReads=0;renderer.displayed=0;

 SETTINGS.trackReadingStats=false;a.renderReadingStatsSleepScreen();if(a.defaults!=1||reads) return 1;
 SETTINGS.trackReadingStats=true;Storage.result=HalStorage::PathStatus::Failed;a.renderReadingStatsSleepScreen();if(a.defaults!=2||reads) return 2;
 Storage.result=HalStorage::PathStatus::Present;bookType=false;a.renderReadingStatsSleepScreen();if(a.defaults!=3||reads) return 3;
 bookType=true;allocation=false;a.renderReadingStatsSleepScreen();if(a.defaults!=4||reads) return 4;
 allocation=true;enabled=false;a.renderReadingStatsSleepScreen();if(a.defaults!=5||renderer.displayed) return 5;
 enabled=true;found=false;a.renderReadingStatsSleepScreen();if(a.defaults!=6||renderer.displayed) return 6;
 found=true;writable=false;a.renderReadingStatsSleepScreen();if(a.defaults!=7||renderer.displayed) return 7;
 writable=true;sleepstats::drawResult=false;a.renderReadingStatsSleepScreen();if(a.defaults!=8||renderer.displayed) return 8;
 sleepstats::drawResult=true;RECENT_BOOKS.books.push_back({APP_STATE.openEpubPath,"Correct title","","Author"});
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

 SETTINGS.trackReadingStats=false;APP_STATE.openEpubPath="/books/example.epub";
 const int oldReads=reads, oldDisplay=renderer.displayed;
 a.renderMinimalBookSleepScreen();
 if(!sleepstats::usedMinimal||sleepstats::progress!=42||sleepstats::seconds!=0||reads!=oldReads||renderer.displayed!=oldDisplay+1) return 25;
 SETTINGS.trackReadingStats=true;enabled=true;found=true;writable=true;
 a.renderMinimalBookSleepScreen();if(sleepstats::seconds!=99||reads!=oldReads+1) return 26;
 APP_STATE.openEpubPath="/books/stale.txt";const int priorProgress=progressReads;
 a.renderMinimalBookSleepScreen();if(sleepstats::progress!=-1||progressReads!=priorProgress) return 27;
 SETTINGS.sleepScreen=CrossPointSettings::DASHBOARD_BOOK_SLEEP;SETTINGS.trackReadingStats=false;
 APP_STATE.openEpubPath="/books/example.epub";a.renderMinimalBookSleepScreen();
 if(!sleepstats::usedDashboard||sleepstats::dashboard.progress!=42||sleepstats::subtitle!="C"||sleepstats::dashboard.hasStats||sleepstats::dashboard.hasGlobal) return 28;
 SETTINGS.trackReadingStats=true;a.renderMinimalBookSleepScreen();
 if(!sleepstats::dashboard.hasStats||!sleepstats::dashboard.hasGlobal||sleepstats::dashboard.remainingSeconds!=50||sleepstats::dashboard.completedBooks!=3||!sleepstats::dashboard.hasDailyAverage||sleepstats::dashboard.dailyAverageSeconds!=49||!sleepstats::dashboard.hasStreak||sleepstats::dashboard.currentStreak!=5) return 29;
 globalMissing=true;a.renderMinimalBookSleepScreen();if(sleepstats::dashboard.hasGlobal) return 30;
 globalMissing=false;clockValid=false;a.renderMinimalBookSleepScreen();
 if(sleepstats::dashboard.hasDailyAverage||sleepstats::dashboard.hasStreak) return 31;
 stubCompleted=true;stubFinished={2026,11,9};a.renderMinimalBookSleepScreen();
 if(!sleepstats::dashboard.hasDailyAverage||sleepstats::dashboard.dailyAverageSeconds!=99||sleepstats::dashboard.remainingSeconds!=0) return 32;
 stubFinished={2026,11,7};a.renderMinimalBookSleepScreen();if(sleepstats::dashboard.hasDailyAverage) return 33;
 SETTINGS.sleepScreen=CrossPointSettings::READING_STATS_SLEEP;SETTINGS.trackReadingStats=false;
 sleepstats::usedMinimal=false;a.renderReadingStatsSleepScreen();if(!sleepstats::usedMinimal||sleepstats::progress!=42) return 34;
 SETTINGS.trackReadingStats=true;enabled=false;sleepstats::usedMinimal=false;
 a.renderReadingStatsSleepScreen();if(!sleepstats::usedMinimal||sleepstats::progress!=42) return 35;



 return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    target = Path(directory)
    (target / 'flow.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(target / 'flow.cpp'), '-o', str(target / 'flow')], check=True)
    subprocess.run([str(target / 'flow')], check=True)
print('Statistics sleep fallback and saved-book flow passed')
