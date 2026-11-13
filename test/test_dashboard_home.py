"""Exercise production read-only Home summary loading, including rich Dashboard data."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
home = (root / 'src/activities/home/HomeActivity.cpp').read_text()
start = home.index('void HomeActivity::loadDashboardData()')
end = home.index('\n#if defined(CROSSPOINT_EXPERIMENTAL_CAROUSEL)', start)
program = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "components/themes/minimal/HomeDashboardData.h"
#include "components/themes/minimal/HomeDashboardDetails.h"
bool ReadingStatsDate::isValid()const{return year>0&&month>0&&day>0;}
bool clockAvailable=true;
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& d){d.date={2026,11,12};return clockAvailable;}
int compareReadingStatsDate(const ReadingStatsDate& a,const ReadingStatsDate& b){return int(a.day)-b.day;}
uint16_t readingSpanDaysElapsed(const ReadingStatsDate& a,const ReadingStatsDate& b){return b.day-a.day;}
struct RecentBook{std::string path;};
struct {bool trackReadingStats=true;} SETTINGS;
struct {bool dashboard=true;bool hasDashboardHome()const{return dashboard;}} GUI;
namespace FsHelpers {
bool hasEpubExtension(const std::string& p){return p.ends_with(".epub");}
bool hasXtcExtension(const std::string& p){return p.ends_with(".xtc");}
bool hasReflowableBookExtension(const std::string& p){return hasEpubExtension(p)||p.ends_with(".txt")||p.ends_with(".md");}
}
int progress=40,progressReads=0,chapterReads=0;
int loadBookProgress(const std::string&,char* chapter=nullptr,size_t size=0){
 ++progressReads;if(chapter){++chapterReads;assert(size==128);std::strcpy(chapter,"A saved chapter");}return progress;}
struct Epub{static std::string cachePathForFilePath(const std::string&,const char*){return "epub-cache";}};
struct Xtc{Xtc(const std::string&,const char*){}std::string getCachePath()const{return "xtc-cache";}};
struct BookReadingStats {
 enum class LoadMode{ReadOnly};bool writable=true,isCompleted=false;
 uint32_t totalReadingSeconds=600,totalPagesTurned=20;uint16_t sessionCount=3;
 ReadingStatsDate startDate{2026,11,10},finishedDate{2026,11,11};
 unsigned remainingReadingSeconds(int p)const{return p==4000?900:0;}
 ReadingStatsDate projectedFinishDate(const ReadingStatsDateTime&,int p)const{assert(p==4000);return {2026,11,15};}
};
BookReadingStats sample;
bool allocationFails=false;
template<class T,class... A>std::unique_ptr<T> makeUniqueNoThrow(A&&... a){if(allocationFails)return {};return std::make_unique<T>(static_cast<A&&>(a)...);}
namespace readingstats {
bool enabled=true,found=true;int reads=0;std::string lastCache;
BookReadingStats loadBook(const std::string& cache,const std::string&,bool& e,bool* f,BookReadingStats::LoadMode mode){assert(mode==BookReadingStats::LoadMode::ReadOnly);++reads;e=enabled;*f=found;lastCache=cache;return sample;}
}
struct HalStorage{enum class PathStatus{Present,Missing,Failed};};
struct {HalStorage::PathStatus status=HalStorage::PathStatus::Present;auto lookupPath(const char*)const{return status;}} Storage;
bool globalWritable=true;int globalReads=0;
struct GlobalReadingStats {
 bool writable=true;uint32_t totalReadingSeconds=7200,completedBooks=4;
 std::array<uint32_t,4>timeOfDaySeconds{1,3,7,2};
 static GlobalReadingStats loadReadOnly(){++globalReads;GlobalReadingStats result;result.writable=globalWritable;return result;}
 uint16_t currentReadingStreak(const ReadingStatsDate* date)const{assert(date->isValid());return 5;}
};
struct HomeActivity {HomeDashboardData dashboardData;HomeDashboardDetails dashboardDetails;std::vector<RecentBook> recentBooks;void loadDashboardData();};
''' + home[start:end] + r'''
int main(){
 HomeActivity h;h.loadDashboardData();assert(h.dashboardData.progress==-1&&readingstats::reads==0);
 h.recentBooks={{"book.epub"}};h.loadDashboardData();
 assert(h.dashboardData.hasStats&&h.dashboardData.progress==40&&h.dashboardData.readingSeconds==600&&h.dashboardData.remainingSeconds==900);
 assert(std::string(h.dashboardDetails.chapter)=="A saved chapter"&&chapterReads==1);
 const auto& d=h.dashboardDetails;
 assert(d.sessions==3&&d.averageSessionSeconds==200&&d.hasPace&&d.pagesPerMinuteTenths==20);
 assert(d.hasClock&&d.hasDays&&d.daysReading==2&&d.dailyAverageSeconds==300&&d.finishDate.day==15);
 assert(d.hasGlobal&&d.globalSeconds==7200&&d.completedBooks==4&&d.currentStreak==5&&d.hasReaderType&&d.readerType==ReadingTimeBucket::Evening);
 sample.isCompleted=true;h.loadDashboardData();assert(h.dashboardData.remainingSeconds==0&&h.dashboardDetails.finishDate.day==11&&h.dashboardDetails.daysReading==1);
 sample.isCompleted=false;sample.sessionCount=0;sample.totalReadingSeconds=60;h.loadDashboardData();assert(!h.dashboardDetails.hasPace&&h.dashboardDetails.averageSessionSeconds==0);
 sample={};sample.startDate.day=14;h.loadDashboardData();assert(!h.dashboardDetails.hasDays);
 sample={};readingstats::enabled=false;h.loadDashboardData();assert(!h.dashboardData.hasStats&&h.dashboardDetails.hasGlobal);
 readingstats::enabled=true;readingstats::found=false;h.loadDashboardData();assert(!h.dashboardData.hasStats&&h.dashboardData.progress==40);
 readingstats::found=true;sample.writable=false;h.loadDashboardData();assert(!h.dashboardData.hasStats);
 sample={};globalWritable=false;h.loadDashboardData();assert(!h.dashboardDetails.hasGlobal);globalWritable=true;
 for(auto status:{HalStorage::PathStatus::Missing,HalStorage::PathStatus::Failed}){Storage.status=status;int n=globalReads;h.loadDashboardData();assert(!h.dashboardDetails.hasGlobal&&globalReads==n);}
 Storage.status=HalStorage::PathStatus::Present;clockAvailable=false;h.loadDashboardData();assert(!h.dashboardDetails.hasClock&&!h.dashboardDetails.hasDays&&h.dashboardDetails.currentStreak==0);
 clockAvailable=true;GUI.dashboard=false;int before=globalReads,chapterBefore=chapterReads;h.loadDashboardData();assert(h.dashboardData.hasStats&&globalReads==before&&chapterReads==chapterBefore);
 GUI.dashboard=true;SETTINGS.trackReadingStats=false;before=readingstats::reads;h.loadDashboardData();assert(!h.dashboardData.hasStats&&h.dashboardData.progress==40&&readingstats::reads==before&&!h.dashboardDetails.hasGlobal);
 SETTINGS.trackReadingStats=true;allocationFails=true;h.loadDashboardData();assert(!h.dashboardData.hasStats&&h.dashboardData.progress==40);allocationFails=false;
 for(const auto& path:{"book.txt","book.md"}){h.recentBooks={{path}};before=progressReads;h.loadDashboardData();assert(h.dashboardData.progress==-1&&h.dashboardData.hasStats&&h.dashboardData.remainingSeconds==0&&h.dashboardDetails.chapter[0]==0&&progressReads==before);}
 h.recentBooks={{"book.xtc"}};h.loadDashboardData();assert(readingstats::lastCache=="xtc-cache"&&h.dashboardData.progress==40);
 h.recentBooks={{"unsupported"}};before=progressReads;h.loadDashboardData();assert(!h.dashboardData.hasStats&&h.dashboardData.progress==-1&&progressReads==before);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    p = Path(tmp)
    (p / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address,undefined', '-I', str(root / 'src'), str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
print('Home summaries load book/device fields read-only with explicit availability')
