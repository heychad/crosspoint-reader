"""Compile the statistics screen against a small tab/list and storage model."""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = root / "src/activities/settings/ReadingStatsActivity.cpp"
keys = sorted(set(re.findall(r"STR_[A-Z0-9_]+", source.read_text())))
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "activities").mkdir()
    (path / "activities/settings").mkdir()
    (path / "activities/reader").mkdir()
    (path / "activities/util").mkdir()
    (path / "components").mkdir()
    (path / "activities/util/KeyboardEntryActivity.h").write_text(r'''
#pragma once
#include <string>
struct GfxRenderer; struct MappedInputManager;
enum class InputType { Text };
class KeyboardEntryActivity {public:
 KeyboardEntryActivity(GfxRenderer&, MappedInputManager&, const char*, const char*, int, InputType){}
};
''')
    (path / "activities/reader/ReaderStats.h").write_text(r'''
#pragma once
#include "activities/reader/BookReadingStats.h"
#include "activities/reader/GlobalReadingStats.h"
class ReaderStats {public:
 int edits=0; bool saveOk=true;
 bool editBookDate(bool started,const ReadingStatsDate& date,BookReadingStats& book,GlobalReadingStats&,bool& saved){
   ++edits; if(started) book.startDate=date; else book.finishedDate=date; saved=saveOk; return true;
 }
};
''')
    (path / "activities/reader/StatsSaveNotice.h").write_text(r'''
#pragma once
inline int retainedFailures=0;
struct StatsSaveNotice { void recordFailure(){++retainedFailures;} };
inline StatsSaveNotice& statsSaveNotice(){static StatsSaveNotice notice;return notice;}
''')
    (path / "Logging.h").write_text('#pragma once\n#define LOG_ERR(...) ((void)0)\n')
    (path / "Memory.h").write_text('#pragma once\n#include <memory>\ntemplate<class T,class... A> auto makeUniqueNoThrow(A&&... a){return std::make_unique<T>(std::forward<A>(a)...);}\n')
    (path / "activities/settings/ReadingChartsActivity.h").write_text(r'''
#pragma once
#include "activities/reader/ReadingChartModel.h"
struct GfxRenderer;struct MappedInputManager;
inline readingcharts::Data chartFixture;
class ReadingChartsActivity {public:
 ReadingChartsActivity(GfxRenderer&,MappedInputManager&,const readingcharts::Data& d,const char*){chartFixture=d;}
};
''')
    (path / "I18n.h").write_text(
        "#pragma once\nenum class StrId {" + ",".join(keys) + "};\n"
        + "struct Language { const char* get(StrId id) const { static const char* names[]={"
        + ",".join('"' + key + '"' for key in keys)
        + "};return names[static_cast<unsigned>(id)];}};\n"
        + "inline Language I18N;\n#define tr(key) I18N.get(StrId::key)\n"
    )
    (path / "CrossPointSettings.h").write_text(
        "#pragma once\nstruct Settings { unsigned char dateFormat=4,dateSeparator=2; };"
        "inline Settings SETTINGS;\n"
    )
    (path / "activities/UiTabListActivity.h").write_text(r'''
#pragma once
#include <cassert>
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <variant>
#include <I18n.h>
struct GfxRenderer {};
struct MappedInputManager {
 enum class Button {Back,Confirm};
 int released=-1;
 bool wasReleased(Button b)const{return released==static_cast<int>(b);}
 struct Labels {const char*btn1;const char*btn2;const char*btn3;const char*btn4;};
 Labels mapLabels(const char*a,const char*b,const char*c,const char*d){return {a,b,c,d};}
};
namespace freeink::ui {
struct Insets {int16_t a,b,c,d;};
struct ListItem {const char*label=nullptr;const char*value=nullptr;int actionValue=0;};
struct Text {int maxLines=0;};
struct ListProps {
 void(*rowProvider)(void*,uint16_t,ListItem&)=nullptr;void*rowProviderCtx=nullptr;
 int count=0,action=0,inputMask=0,valueInset=0;Text labelText;
};
constexpr int InputTouch=1;
}
struct UiScreen {
 struct Theme {freeink::ui::Text smallText;};
 void setContentMarginFromScreen(freeink::ui::Insets){} void spacer(int16_t){}
 Theme theme(){return{};} void list(freeink::ui::ListProps){}
};
inline int lockDepth=0,waitCount=0;inline void(*waitHook)()=nullptr;
struct RenderLock {template<typename T> explicit RenderLock(T&){++lockDepth;}~RenderLock(){--lockDepth;} };
struct KeyboardResult {std::string text;};
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,KeyboardResult> data;};
class UiTabListActivity {
 public:
 struct App {void clearTapFlash(){}} app;
 GfxRenderer& renderer;MappedInputManager& mappedInput;
 bool finished=false,updated=false;int ring=0;
 static constexpr int ACTION_ROW=1;
 UiTabListActivity(const char*,GfxRenderer&r,MappedInputManager&i):renderer(r),mappedInput(i){}
 virtual ~UiTabListActivity()=default;virtual void onEnter(){} virtual void onExit(){} virtual void prepareForSleep(){} virtual void loop(){}
 virtual int listCount()const=0;virtual int tabCount()const=0;virtual int activeTab()const=0;
 virtual const char*tabLabel(int)const=0;virtual void onTabAction(int)=0;virtual void stepTab(int)=0;
 virtual bool handleButtons()=0;virtual void buildScreen(UiScreen&)=0;virtual void activateIndex(int)=0;
 virtual const char*headerTitle()const=0;virtual void drawFooter()=0;
 void buildTabBar(UiScreen&){} void syncTabListViewport(UiScreen&,freeink::ui::ListProps&){}
 void moveRingTo(int n){ring=n;updated=true;} int ringPos()const{return ring;}
 void requestUpdate(){updated=true;}
 void requestUpdateAndWait(){assert(lockDepth==0);++waitCount;if(waitHook)waitHook();updated=true;}
 void finish(){finished=true;}
 template<class T,class F> void startActivityForResult(std::unique_ptr<T>,F callback){++launched;handler=callback;}
 std::function<void(const ActivityResult&)> handler;
 int launched=0;
};
''')
    (path / "components/UITheme.h").write_text(r'''
#pragma once
#include "activities/UiTabListActivity.h"
struct UITheme {
 struct Metrics {int topPadding=1,headerHeight=1,buttonHintsHeight=1,verticalSpacing=1;};
 static UITheme&getInstance(){static UITheme theme;return theme;}
 Metrics getMetrics()const{return{};}
};
struct Gui {void drawButtonHints(GfxRenderer&,const char*,const char*,const char*,const char*){}};
inline Gui GUI;
''')
    harness = r'''
#include <array>
#include <string>
#include <cstdint>
#include <cstdio>
#include <cassert>
#include <algorithm>
#include "activities/UiTabListActivity.h"
#define private public
#include "activities/settings/ReadingStatsActivity.h"
#undef private
#include "activities/settings/ReadingChartsActivity.h"
#include "activities/reader/BookTracking.h"
#include "activities/reader/ReaderStats.h"
#include "activities/reader/StatsSaveNotice.h"
booktracking::State trackingFixture=booktracking::State::Disabled;bool trackingWriteOk=true;int trackingWrites=0;
namespace booktracking {State state(const std::string&,const std::string&){return trackingFixture;}
bool setEnabled(const std::string&,const std::string&,bool on){++trackingWrites;if(!trackingWriteOk)return false;trackingFixture=on?State::Enabled:State::Disabled;return true;}}
GlobalReadingStats localFixture;
GlobalStatsAggregate aggregateFixture;
int backupCalls=0;bool backupResult=true;
int closedDateEdits=0;bool closedDateSaveOk=true;
namespace readingstats {
bool editClosedBookDate(const std::string&,const std::string&,bool started,const ReadingStatsDate& date,BookReadingStats& book){
 ++closedDateEdits;if(!closedDateSaveOk)return false;if(started)book.startDate=date;else book.finishedDate=date;return true;
}}
namespace statsbackup {bool createDeviceBackup(const GlobalReadingStats&){++backupCalls;return backupResult;}
bool createBookBackup(const std::string&,const BookReadingStats&){++backupCalls;return backupResult;}}
ReadingStatsActivity* waitingScreen=nullptr;
void verifySaving(){assert(waitingScreen && waitingScreen->backupStatus==ReadingStatsActivity::BackupStatus::Saving);}
bool clockAvailable=true, historyCovered=true;
GlobalReadingStats GlobalReadingStats::load(){return localFixture;}
GlobalStatsAggregate GlobalReadingStats::loadAggregated(const GlobalReadingStats&){return aggregateFixture;}
uint16_t GlobalReadingStats::currentReadingStreak(const ReadingStatsDate*)const{return 7;}
uint16_t GlobalReadingStats::displayLongestReadingStreak()const{return 12;}
void BookReadingStats::formatDuration(uint32_t n,char*out,size_t cap){snprintf(out,cap,"%u seconds",n);}
bool ReadingStatsDate::isValid()const{return year && month && day;}
bool readingHistoryCoversWindow(uint32_t,const ReadingStatsDate& date,uint16_t){return historyCovered && date.isValid();}
uint16_t recordedReadingDays(uint32_t,const std::array<uint8_t,READING_HISTORY_BYTES>&,const ReadingStatsDate&,uint16_t window){return window==7 ? 4 : 15;}
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime&out){out.date={2026,11,4};return clockAvailable;}
uint32_t BookReadingStats::remainingReadingSeconds(int)const{return writable&&!isCompleted?estimatedTimeLeftSeconds:0;}
ReadingStatsDate BookReadingStats::projectedFinishDate(const ReadingStatsDateTime&,int)const{return {};}
uint16_t readingSpanDaysElapsed(const ReadingStatsDate&,const ReadingStatsDate&){return 5;}
std::string value(ReadingStatsActivity&screen,ReadingStatsActivity::Row row){
 freeink::ui::ListItem item;screen.formatRow(row,item);return item.value;
}
int main(){
 GfxRenderer renderer;MappedInputManager input;
 localFixture.totalReadingSeconds=300;localFixture.totalSessions=3;
 aggregateFixture={localFixture,2,0,true};aggregateFixture.stats.totalReadingSeconds=500;
 ReadingStatsActivity device(renderer,input);device.onEnter();
 assert(device.tabCount()==2 && device.listCount()==25);
 assert(value(device,ReadingStatsActivity::Row::RecordedWeek)=="4");
 assert(value(device,ReadingStatsActivity::Row::RecordedMonth)=="15");
 historyCovered=false;
 assert(value(device,ReadingStatsActivity::Row::RecordedWeek)=="-");
 assert(value(device,ReadingStatsActivity::Row::RecordedMonth)=="-");
 historyCovered=true;
 assert(value(device,ReadingStatsActivity::Row::Time)=="300 seconds");
 int backupRow=-1;
 for(int i=0;i<device.listCount();++i)if(device.rowAt(i)==ReadingStatsActivity::Row::BackupDevice)backupRow=i;
 waitingScreen=&device;waitHook=verifySaving;
 assert(backupRow>=0);device.activateIndex(backupRow);assert(waitCount==0 && backupCalls==0);
 device.loop();assert(waitCount==1);
 assert(backupCalls==1 && value(device,ReadingStatsActivity::Row::BackupDevice)=="STR_STATS_BACKUP_SAVED");
 backupResult=false;device.activateIndex(backupRow);device.loop();
 assert(waitCount==2 && backupCalls==2 && value(device,ReadingStatsActivity::Row::BackupDevice)=="STR_STATS_BACKUP_FAILED");
 device.local.writable=false;device.activateIndex(backupRow);
 assert(backupCalls==2 && value(device,ReadingStatsActivity::Row::BackupDevice)=="STR_STATS_BACKUP_UNAVAILABLE");
 device.local.writable=true;
 device.onTabAction(1);assert(device.activeTab()==1);
 assert(device.listCount()==22);
 assert(value(device,ReadingStatsActivity::Row::Time)=="500 seconds");
 device.aggregate.complete=false;device.aggregate.skipped=1;
 assert(value(device,ReadingStatsActivity::Row::State)=="STR_STATS_PARTIAL");
 device.onTabAction(99);assert(device.activeTab()==1);
 device.stepTab(1);assert(device.activeTab()==0);
 input.released=static_cast<int>(MappedInputManager::Button::Confirm);
 device.ring=2;device.handleButtons();assert(device.activeTab()==0);
 device.ring=0;device.handleButtons();assert(device.activeTab()==1);
 BookReadingStats stats;stats.totalReadingSeconds=120;stats.sessionCount=2;
 stats.totalPagesTurned=12;stats.startDate={2026,10,1};stats.timeOfDaySeconds[0]=90;
 ReadingStatsActivity book(renderer,input,"The Book",stats,localFixture,3888,false,true,"cache","book",nullptr);book.onEnter();
 waitHook=nullptr;book.loop();
 assert(book.tabCount()==3 && book.listCount()==28);
 freeink::ui::ListItem titleRow;book.formatRow(ReadingStatsActivity::Row::Title,titleRow);
 assert(std::string(titleRow.label)=="The Book" && titleRow.value==nullptr);
 assert(value(book,ReadingStatsActivity::Row::Progress)=="38.88%");
 assert(value(book,ReadingStatsActivity::Row::AverageSession)=="60 seconds");
 assert(value(book,ReadingStatsActivity::Row::PagesPerMinute)=="6.0");
 assert(value(book,ReadingStatsActivity::Row::Started)=="2026-10-01");
 ReaderStats live;
 ReadingStatsActivity editable(renderer,input,"The Book",stats,localFixture,3888,true,true,"cache","book",&live);
 editable.onEnter();
 int startedRow=-1,finishedRow=-1;
 for(int i=0;i<editable.listCount();++i){
  if(editable.rowAt(i)==ReadingStatsActivity::Row::Started)startedRow=i;
  if(editable.rowAt(i)==ReadingStatsActivity::Row::Finished)finishedRow=i;
 }
 assert(startedRow>=0 && finishedRow>=0);
 editable.activateIndex(startedRow);assert(editable.launched==1 && live.edits==0);
 editable.handler(ActivityResult{false,KeyboardResult{"2024-02-29"}});
 assert(live.edits==1 && value(editable,ReadingStatsActivity::Row::Started)=="2024-02-29");
 editable.activateIndex(finishedRow);
 editable.handler(ActivityResult{false,KeyboardResult{"not-a-date"}});
 assert(live.edits==1 && value(editable,ReadingStatsActivity::Row::Finished)=="STR_STATS_INVALID_DATE");
 editable.activateIndex(finishedRow);editable.handler(ActivityResult{true,{}});assert(live.edits==1);
 live.saveOk=false;editable.activateIndex(finishedRow);
 editable.handler(ActivityResult{false,KeyboardResult{"2024-04-05"}});
 assert(live.edits==2 && value(editable,ReadingStatsActivity::Row::Finished)=="STR_STATS_SAVE_INCOMPLETE");
 ReadingStatsActivity closed(renderer,input,"The Book",stats,localFixture,3888,true,true,"cache","book",nullptr,true);
 closed.onEnter();closed.activateIndex(startedRow);assert(closed.launched==1);
 closed.handler(ActivityResult{false,KeyboardResult{"2024-02-29"}});
 assert(closedDateEdits==0 && value(closed,ReadingStatsActivity::Row::Started)=="STR_STATS_BACKUP_SAVING");
 closed.loop();
 assert(closedDateEdits==1 && value(closed,ReadingStatsActivity::Row::Started)=="2024-02-29");
 closedDateSaveOk=false;closed.activateIndex(finishedRow);
 closed.handler(ActivityResult{false,KeyboardResult{"2024-04-05"}});
 closed.loop();
 assert(closedDateEdits==2 && value(closed,ReadingStatsActivity::Row::Finished)=="STR_STATS_CLOSED_DATE_CHECK");
 closedDateSaveOk=true;closed.activateIndex(startedRow);
 closed.handler(ActivityResult{false,KeyboardResult{"2024-03-01"}});
 closed.prepareForSleep();assert(closedDateEdits==3 && !closed.closedDateSavePending);
 closed.onExit();assert(closedDateEdits==3);
 closedDateSaveOk=false;closed.activateIndex(startedRow);
 closed.handler(ActivityResult{false,KeyboardResult{"2024-03-02"}});
 closed.onExit();assert(closedDateEdits==4 && retainedFailures==1);
 book.activateIndex(startedRow);assert(book.launched==0);

 assert(value(book,ReadingStatsActivity::Row::Finished)=="-");
 assert(value(book,ReadingStatsActivity::Row::Morning)=="90 seconds");
 assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_OFF");
 int bookBackupRow=-1;for(int i=0;i<book.listCount();++i)if(book.rowAt(i)==ReadingStatsActivity::Row::BackupBook)bookBackupRow=i;
 assert(bookBackupRow>=0);int priorBackupCalls=backupCalls;backupResult=true;
 book.activateIndex(bookBackupRow);assert(backupCalls==priorBackupCalls);book.loop();
 assert(backupCalls==priorBackupCalls+1 && value(book,ReadingStatsActivity::Row::BackupBook)=="STR_STATS_BACKUP_SAVED");
 book.saved=false;book.activateIndex(bookBackupRow);assert(value(book,ReadingStatsActivity::Row::BackupBook)=="STR_STATS_BACKUP_UNAVAILABLE");book.saved=true;

 int trackingRow=-1;for(int i=0;i<book.listCount();++i)if(book.rowAt(i)==ReadingStatsActivity::Row::Tracking)trackingRow=i;
 assert(trackingRow>=0);book.activateIndex(trackingRow);assert(trackingWrites==0);book.loop();
 assert(trackingWrites==1 && value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_ON");
 trackingWriteOk=false;book.activateIndex(trackingRow);book.loop();
 assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_CHECK_AGAIN");
 book.activateIndex(trackingRow);book.loop();assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_ON");
 for(int tab=0;tab<book.tabCount();++tab){
  book.onTabAction(tab);
  for(int row=0;row<book.listCount();++row){freeink::ui::ListItem item;book.provideRow(&book,row,item);assert(item.label);assert(item.value || (tab==0 && row==0) || book.rowAt(row)==ReadingStatsActivity::Row::Charts || book.rowAt(row)==ReadingStatsActivity::Row::BackupDevice || book.rowAt(row)==ReadingStatsActivity::Row::BackupBook);}
  assert(book.rowAt(book.listCount()-1)==ReadingStatsActivity::Row::Sunday);
  UiScreen ui;book.buildScreen(ui);book.drawFooter();
 }
 book.onTabAction(0);book.saved=false;
 book.ring=17;input.released=static_cast<int>(MappedInputManager::Button::Confirm);book.handleButtons();
 assert(book.launched==1 && chartFixture.scope==readingcharts::Scope::Book && chartFixture.warning==readingcharts::Warning::Unsaved);
 assert(value(book,ReadingStatsActivity::Row::State)=="STR_STATS_SAVE_INCOMPLETE");
 book.book.writable=false;
 assert(value(book,ReadingStatsActivity::Row::State)=="STR_STATS_DATA_UNAVAILABLE");
 assert(value(book,ReadingStatsActivity::Row::Pages)=="-");
 assert(value(book,ReadingStatsActivity::Row::Morning)=="-");
 assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_ON");
 ReadingStatsActivity limit(renderer,input,"B",stats,localFixture,20000,true,true,"cache","book",nullptr);
 assert(value(limit,ReadingStatsActivity::Row::Progress)=="100.00%");
 std::string longTitle(157,'x');longTitle+="€";
 ReadingStatsActivity truncated(renderer,input,longTitle,stats,localFixture,100,true,true,"cache","book",nullptr);
 assert(std::string(truncated.bookTitle)==std::string(157,'x'));
 input.released=static_cast<int>(MappedInputManager::Button::Back);
 assert(book.handleButtons() && book.finished);
}
'''
    (path / "test.cpp").write_text(harness)
    command = ["c++", "-std=c++20", "-I", str(path), "-I", str(root / "src"), "-I", str(root / "lib/I18n"),
               str(source), str(root / "src/activities/reader/ReadingStatsDateEdit.cpp"), str(path / "test.cpp"), "-o", str(path / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(path / "test")], check=True)
print("Statistics tab, row, unknown-data and navigation checks passed")
