"""Compile the statistics screen against a small tab/list and storage model."""
from pathlib import Path
import os
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
    (path / "activities/home").mkdir()
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
    (path / "activities/home/FileBrowserActivity.h").write_text(r'''
#pragma once
#include <string>
struct GfxRenderer; struct MappedInputManager;
class FileBrowserActivity {public:
 enum class Mode {PickStatsBackup};
 FileBrowserActivity(GfxRenderer&,MappedInputManager&,const std::string&,Mode){}
};
''')
    (path / "activities/util/ConfirmationActivity.h").write_text(r'''
#pragma once
#include <string>
struct GfxRenderer; struct MappedInputManager;
inline std::string confirmationBody;
class ConfirmationActivity {public:
 ConfirmationActivity(GfxRenderer&,MappedInputManager&,const char*,const std::string& body,const char*,const char*){confirmationBody=body;}
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
        "#pragma once\nstruct Settings { unsigned char dateFormat=4,dateSeparator=2,removeReadBooksFromRecents=0,moveFinishedToReadFolder=0; };"
        "inline Settings SETTINGS;\n"
    )
    (path / "RecentBooksStore.h").write_text(r'''
#pragma once
#include <string>
extern int recentCompletionCalls;extern bool recentCompletionOk;
struct RecentBooksStore {bool removeByPathPersisted(const std::string&){++recentCompletionCalls;return recentCompletionOk;}};
inline RecentBooksStore RECENT_BOOKS;
''')
    (path / "FsHelpers.h").write_text(r'''
#pragma once
#include <string>
namespace FsHelpers {inline bool hasEpubExtension(const std::string& path){return path.ends_with(".epub");}}
''')
    (path / "Epub.h").write_text(r'''
#pragma once
#include <string>
struct Epub {static std::string cachePathForFilePath(const std::string& path,const std::string&){return "cache:"+path;}};
''')
    (path / "HalStorage.h").write_text(r'''
#pragma once
struct HalStorage {enum class PathStatus {Missing,Present,Failed};PathStatus status=PathStatus::Missing;
 PathStatus lookupPath(const char*){return status;}};
inline HalStorage Storage;
''')
    (path / "activities/reader/CompletionMovePlanIO.h").write_text(r'''
#pragma once
#include <string>
namespace readingstats::completionmove {
enum class PlanResult {Ready,Conflict};
inline PlanResult planResult=PlanResult::Ready;
inline PlanResult preparePlan(const std::string&,const std::string&){return planResult;}
}
''')
    (path / "activities/reader/CompletionMoveReferences.h").write_text(r'''
#pragma once
namespace readingstats::completionmove {
enum class ReferenceResult {Updated,RecoveryRequired};
inline ReferenceResult referenceResult=ReferenceResult::Updated;
inline ReferenceResult updateMoveReferences(){return referenceResult;}
}
''')
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
struct FilePathResult {std::string path;};
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,KeyboardResult,FilePathResult> data;};
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
#include "util/ReadFolder.h"
#undef private
#include "activities/settings/ReadingChartsActivity.h"
#include "activities/reader/BookTracking.h"
#include "activities/reader/ReaderStats.h"
#include "activities/reader/StatsSaveNotice.h"
#include "CrossPointSettings.h"
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) || defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
#include "activities/reader/CompletionTransaction.h"
#include "activities/reader/ReadingStatsStore.h"
#endif
booktracking::State trackingFixture=booktracking::State::Disabled;bool trackingWriteOk=true;int trackingWrites=0;
namespace booktracking {State state(const std::string&,const std::string&){return trackingFixture;}
bool setEnabled(const std::string&,const std::string&,bool on){++trackingWrites;if(!trackingWriteOk)return false;trackingFixture=on?State::Enabled:State::Disabled;return true;}}
GlobalReadingStats localFixture;
GlobalStatsAggregate aggregateFixture;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
BookReadingStats completionBookFixture;
int completionCalls=0;
int completionRecoveryCalls=0;
int completionProbeCalls=0;
int completionFinalizeCalls=0;
int recentCompletionCalls=0;bool recentCompletionOk=true;
readingstats::ClosedCompletionResult completionResult=readingstats::ClosedCompletionResult::Applied;
readingstats::CompletionRecoveryResult completionRecoveryResult=readingstats::CompletionRecoveryResult::NoPending;
readingstats::CompletionProbeResult completionProbeResult=readingstats::CompletionProbeResult::NoPending;
bool completionProbeTarget=false;
uint8_t completionProbePolicy=0;
namespace readingstats {
ClosedCompletionResult setClosedBookCompletionStats(const std::string&,bool completed,const ReadingStatsDate&,uint8_t){
 ++completionCalls; if(completionResult==ClosedCompletionResult::Applied)completionBookFixture.isCompleted=completed;
 return completionResult;
}
CompletionRecoveryResult recoverClosedBookCompletionStats(const std::string&,bool completed){
 ++completionRecoveryCalls;
 if(completionRecoveryResult==CompletionRecoveryResult::Recovered)completionBookFixture.isCompleted=completed;
 return completionRecoveryResult;
}
CompletionFinalizeResult finalizeClosedBookCompletion(const std::string&,bool){
 ++completionFinalizeCalls;return CompletionFinalizeResult::Finalized;
}
CompletionProbeResult probeClosedBookCompletion(const std::string&,bool& target,uint8_t* policy){
 if(policy)*policy=completionProbePolicy;
 ++completionProbeCalls;if(completionProbeResult==CompletionProbeResult::Matching)target=completionProbeTarget;
 return completionProbeResult;
}
std::string loadedCompletionCache,loadedCompletionPath;
BookReadingStats loadBook(const std::string& cache,const std::string& path,bool& enabled,bool* found,BookReadingStats::LoadMode,bool* known){
 loadedCompletionCache=cache;loadedCompletionPath=path;
 enabled=true;*found=true;*known=true;return completionBookFixture;
}
}
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE) && !defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
int completionRecoveryCalls=0,completionFinalizeCalls=0;
readingstats::CompletionRecoveryResult completionRecoveryResult=readingstats::CompletionRecoveryResult::Recovered;
readingstats::CompletionProbeResult completionProbeResult=readingstats::CompletionProbeResult::Matching;
bool completionProbeTarget=false;
uint8_t completionProbePolicy=0;
namespace readingstats {
CompletionRecoveryResult recoverClosedBookCompletionStats(const std::string&,bool){++completionRecoveryCalls;return completionRecoveryResult;}
CompletionFinalizeResult finalizeClosedBookCompletion(const std::string&,bool){++completionFinalizeCalls;return CompletionFinalizeResult::Finalized;}
CompletionProbeResult probeClosedBookCompletion(const std::string&,bool& target,uint8_t* policy){
 target=completionProbeTarget;if(policy)*policy=completionProbePolicy;return completionProbeResult;
}
BookReadingStats loadBook(const std::string&,const std::string&,bool& enabled,bool* found,BookReadingStats::LoadMode,bool* known){
 enabled=true;*found=true;*known=true;BookReadingStats result;result.totalReadingSeconds=100;return result;
}
}
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION) || defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
GlobalReadingStats GlobalReadingStats::loadReadOnly(){return localFixture;}
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
#include "activities/reader/DeviceStatsBackup.h"
#include "activities/reader/BookStatsBackup.h"
#include "activities/util/ConfirmationActivity.h"
GlobalReadingStats selectedFixture;
bool backupReadable=true;int restoreCalls=0;
bool legacyBackup=false;
bool bookSelectedFinished=false;
int bookRestoreCalls=0;
int resetCalls=0;
statsbackup::BookRestoreResult bookRestoreResult=statsbackup::BookRestoreResult::Restored;
statsbackup::RestoreResult resetResult=statsbackup::RestoreResult::Restored;
statsbackup::RestoreResult restoreResult=statsbackup::RestoreResult::Restored;
bool GlobalReadingStats::encode(std::array<uint8_t,CURRENT_FILE_SIZE>& bytes)const {
 if(!writable)return false;bytes={};bytes[0]=totalReadingSeconds & 255;bytes[1]=(totalReadingSeconds>>8)&255;return true;
}
bool BookReadingStats::encode(uint8_t* bytes,size_t size)const {
 if(!writable || size!=CURRENT_FILE_SIZE)return false;std::fill(bytes,bytes+size,0);bytes[0]=totalReadingSeconds & 255;return true;
}
namespace statsbackup {
BackupIntegrity inspectDeviceBackup(const std::string& name,GlobalReadingStats& output){
 if(!backupReadable || name!="chosen.bin")return BackupIntegrity::Unavailable;
 output=selectedFixture;return legacyBackup?BackupIntegrity::LegacyUnverified:BackupIntegrity::Checked;
}
bool readDeviceBackup(const std::string& name,GlobalReadingStats& output){
 if(!backupReadable || name!="chosen.bin")return false;output=selectedFixture;return true;
}
RestoreResult restoreDeviceBackup(const std::string&,const uint8_t* current,size_t currentSize,const uint8_t* backup,size_t backupSize){
 ++restoreCalls;assert(current && currentSize==GlobalReadingStats::CURRENT_FILE_SIZE);
 assert(backup && backupSize==GlobalReadingStats::CURRENT_FILE_SIZE);
 if(restoreResult==RestoreResult::Restored)localFixture=selectedFixture;return restoreResult;
}
RestoreResult resetDeviceStats(const uint8_t* current,size_t currentSize){
 ++resetCalls;assert(current && currentSize==GlobalReadingStats::CURRENT_FILE_SIZE);
 if(resetResult==RestoreResult::Restored)localFixture={};return resetResult;
}
bool readBookBackup(const std::string&,const std::string& name,BookReadingStats& output){
 if(!backupReadable || name!="chosen.bin")return false;output.totalReadingSeconds=200;output.isCompleted=bookSelectedFinished;return true;
}
BookRestoreResult restoreBookBackup(const std::string&,const std::string&,const std::string&,
                                    const uint8_t* current,size_t currentSize,const uint8_t* backup,size_t backupSize){
 ++bookRestoreCalls;assert(current && currentSize==BookReadingStats::CURRENT_FILE_SIZE);
 assert(backup && backupSize==BookReadingStats::CURRENT_FILE_SIZE);return bookRestoreResult;
}
}
#endif
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
uint16_t recordedReadingDays(uint32_t,const std::array<uint8_t,READING_HISTORY_BYTES>&,const ReadingStatsDate&,uint16_t window){return window==7 ? 4 : window==30 ? 15 : 88;}
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime&out){out.date={2026,11,4};return clockAvailable;}
uint32_t BookReadingStats::remainingReadingSeconds(int)const{return writable&&!isCompleted?estimatedTimeLeftSeconds:0;}
ReadingStatsDate BookReadingStats::projectedFinishDate(const ReadingStatsDateTime&,int)const{return {};}
uint16_t readingSpanDaysElapsed(const ReadingStatsDate&,const ReadingStatsDate&){return 5;}
std::string value(ReadingStatsActivity&screen,ReadingStatsActivity::Row row){
 freeink::ui::ListItem item;screen.formatRow(row,item);return item.value;
}
int main(){
 GfxRenderer renderer;MappedInputManager input;
 localFixture.totalReadingSeconds=300;localFixture.totalSessions=3;localFixture.totalPagesTurned=30;
 aggregateFixture={localFixture,2,0,true};aggregateFixture.stats.totalReadingSeconds=500;
 ReadingStatsActivity device(renderer,input);device.onEnter();
 #if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 assert(device.tabCount()==2 && device.listCount()==29);
#else
 assert(device.tabCount()==2 && device.listCount()==27);
#endif
 assert(value(device,ReadingStatsActivity::Row::RecordedWeek)=="4");
 assert(value(device,ReadingStatsActivity::Row::PagesPerMinute)=="6.0");
 assert(value(device,ReadingStatsActivity::Row::RecordedMonth)=="15");
 assert(value(device,ReadingStatsActivity::Row::RecordedYear)=="88");
 historyCovered=false;
 assert(value(device,ReadingStatsActivity::Row::RecordedWeek)=="-");
 assert(value(device,ReadingStatsActivity::Row::RecordedMonth)=="-");
 assert(value(device,ReadingStatsActivity::Row::RecordedYear)=="-");
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
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 waitHook=nullptr;int restoreRow=-1;
 for(int i=0;i<device.listCount();++i)if(device.rowAt(i)==ReadingStatsActivity::Row::RestoreDevice)restoreRow=i;
 assert(restoreRow>=0);selectedFixture.totalReadingSeconds=400;
 device.activateIndex(restoreRow);assert(device.launched==1);
 {auto callback=std::move(device.handler);ActivityResult cancelled;cancelled.isCancelled=true;callback(cancelled);}
 assert(restoreCalls==0 && !device.restorePending);
 device.activateIndex(restoreRow);
 {auto callback=std::move(device.handler);ActivityResult bad;bad.data=FilePathResult{"/outside/chosen.bin"};callback(bad);}
 assert(restoreCalls==0 && device.restoreStatus==ReadingStatsActivity::BackupStatus::Failed);
 device.activateIndex(restoreRow);
 legacyBackup=true;
 {auto callback=std::move(device.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/chosen.bin"};callback(chosen);}
 assert(confirmationBody.find("STR_STATS_RESTORE_LEGACY_NOTE")!=std::string::npos);
 assert(device.launched==4 && restoreCalls==0);
 {auto callback=std::move(device.handler);ActivityResult cancel;cancel.isCancelled=true;callback(cancel);}
 legacyBackup=false;
 assert(!device.restorePending && restoreCalls==0);
 device.activateIndex(restoreRow);
 {auto callback=std::move(device.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/chosen.bin"};callback(chosen);}
 {auto callback=std::move(device.handler);ActivityResult confirm;callback(confirm);}
 assert(device.restorePending && restoreCalls==0);device.loop();
 assert(restoreCalls==1 && !device.restorePending && device.local.totalReadingSeconds==400);
 assert(value(device,ReadingStatsActivity::Row::RestoreDevice)=="STR_STATS_RESTORE_DONE");
 localFixture.totalReadingSeconds=300;device.local=localFixture;
 int resetRow=-1;for(int i=0;i<device.listCount();++i)if(device.rowAt(i)==ReadingStatsActivity::Row::ResetDevice)resetRow=i;
 assert(resetRow>=0);device.activateIndex(resetRow);
 {auto callback=std::move(device.handler);ActivityResult cancel;cancel.isCancelled=true;callback(cancel);}
 assert(resetCalls==0 && !device.resetPending);
 device.activateIndex(resetRow);
 {auto callback=std::move(device.handler);ActivityResult confirm;callback(confirm);}
 assert(device.resetPending && resetCalls==0);device.loop();
 assert(resetCalls==1 && !device.resetPending && device.local.totalReadingSeconds==0);
 assert(value(device,ReadingStatsActivity::Row::ResetDevice)=="STR_STATS_RESET_DONE");
 localFixture.totalReadingSeconds=300;device.local=localFixture;
 device.activateIndex(restoreRow);
 {auto callback=std::move(device.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/chosen.bin"};callback(chosen);}
 {auto callback=std::move(device.handler);ActivityResult confirm;callback(confirm);}
 backupReadable=false;device.loop();backupReadable=true;
 assert(restoreCalls==1 && device.restoreStatus==ReadingStatsActivity::BackupStatus::Failed);
 device.activateIndex(restoreRow);
 {auto callback=std::move(device.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/chosen.bin"};callback(chosen);}
 {auto callback=std::move(device.handler);ActivityResult confirm;callback(confirm);}
 selectedFixture.totalReadingSeconds=401;device.loop();selectedFixture.totalReadingSeconds=400;
 assert(restoreCalls==1 && device.restoreStatus==ReadingStatsActivity::BackupStatus::Failed);
 device.activateIndex(restoreRow);
 {auto callback=std::move(device.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/chosen.bin"};callback(chosen);}
 {auto callback=std::move(device.handler);ActivityResult confirm;callback(confirm);}
 const int priorFailures=retainedFailures;restoreResult=statsbackup::RestoreResult::WriteUncertain;device.loop();
 assert(restoreCalls==2 && retainedFailures==priorFailures+1);
 assert(value(device,ReadingStatsActivity::Row::RestoreDevice)=="STR_STATS_RESTORE_UNCERTAIN");
 restoreResult=statsbackup::RestoreResult::Restored;retainedFailures=priorFailures;
#endif
 device.onTabAction(1);assert(device.activeTab()==1);
 assert(device.listCount()==26);
 assert(value(device,ReadingStatsActivity::Row::Time)=="500 seconds");
 assert(value(device,ReadingStatsActivity::Row::PagesPerMinute)=="3.6");
 assert(value(device,ReadingStatsActivity::Row::RecordedWeek)=="4");
 assert(value(device,ReadingStatsActivity::Row::RecordedMonth)=="15");
 assert(value(device,ReadingStatsActivity::Row::RecordedYear)=="88");
 device.aggregate.complete=false;device.aggregate.skipped=1;
 assert(value(device,ReadingStatsActivity::Row::State)=="STR_STATS_PARTIAL");
 device.saved=false;
 assert(value(device,ReadingStatsActivity::Row::State)=="STR_STATS_SAVE_PARTIAL");
 device.openCharts();
 assert(chartFixture.scope==readingcharts::Scope::AllDevices && chartFixture.warning==readingcharts::Warning::UnsavedPartial);
 device.saved=true;
 device.onTabAction(99);assert(device.activeTab()==1);
 device.stepTab(1);assert(device.activeTab()==0);
 input.released=static_cast<int>(MappedInputManager::Button::Confirm);
 device.ring=2;device.handleButtons();assert(device.activeTab()==0);
 device.ring=0;device.handleButtons();assert(device.activeTab()==1);
 BookReadingStats stats;stats.totalReadingSeconds=120;stats.sessionCount=2;
 stats.totalPagesTurned=12;stats.startDate={2026,10,1};stats.timeOfDaySeconds[0]=90;
 ReadingStatsActivity book(renderer,input,"The Book",stats,localFixture,3888,false,true,"cache","book",nullptr);book.onEnter();
 waitHook=nullptr;book.loop();
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 int bookRestoreRow=-1;for(int i=0;i<book.listCount();++i)if(book.rowAt(i)==ReadingStatsActivity::Row::RestoreBook)bookRestoreRow=i;
 assert(bookRestoreRow>=0);book.activateIndex(bookRestoreRow);assert(book.launched==0);
 ReadingStatsActivity closedRestore(renderer,input,"The Book",stats,localFixture,3888,false,true,"cache","book",nullptr,true);
 closedRestore.onEnter();closedRestore.activateIndex(bookRestoreRow);assert(closedRestore.launched==1);
 {auto callback=std::move(closedRestore.handler);ActivityResult cancel;cancel.isCancelled=true;callback(cancel);}
 assert(!closedRestore.bookRestorePending && bookRestoreCalls==0);
 closedRestore.activateIndex(bookRestoreRow);
 {auto callback=std::move(closedRestore.handler);ActivityResult wrong;wrong.data=FilePathResult{"/outside/chosen.bin"};callback(wrong);}
 assert(closedRestore.bookRestoreStatus==ReadingStatsActivity::BackupStatus::Failed);
 closedRestore.activateIndex(bookRestoreRow);
 bookSelectedFinished=true;
 {auto callback=std::move(closedRestore.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/books/cache/chosen.bin"};callback(chosen);}
 assert(confirmationBody.find("STR_STATS_RESTORE_FINISHED_NOTE")!=std::string::npos);
 {auto callback=std::move(closedRestore.handler);ActivityResult cancel;cancel.isCancelled=true;callback(cancel);}
 bookSelectedFinished=false;
 assert(!closedRestore.bookRestorePending && bookRestoreCalls==0);
 closedRestore.activateIndex(bookRestoreRow);
 {auto callback=std::move(closedRestore.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/books/cache/chosen.bin"};callback(chosen);}
 {auto callback=std::move(closedRestore.handler);ActivityResult confirm;callback(confirm);}
 assert(closedRestore.bookRestorePending && bookRestoreCalls==0);closedRestore.loop();
 assert(!closedRestore.bookRestorePending && bookRestoreCalls==1 && closedRestore.bookRestoreStatus==ReadingStatsActivity::BackupStatus::Saved);
 closedRestore.activateIndex(bookRestoreRow);
 {auto callback=std::move(closedRestore.handler);ActivityResult chosen;chosen.data=FilePathResult{"/.crossink-stats-backup/books/cache/chosen.bin"};callback(chosen);}
 {auto callback=std::move(closedRestore.handler);ActivityResult confirm;callback(confirm);}
 const int priorBookFailures=retainedFailures;bookRestoreResult=statsbackup::BookRestoreResult::RecoveryRequired;closedRestore.loop();
 assert(bookRestoreCalls==2 && retainedFailures==priorBookFailures+1 && closedRestore.bookRestoreStatus==ReadingStatsActivity::BackupStatus::Unavailable);
 completionProbeResult=readingstats::CompletionProbeResult::Matching;completionProbePolicy=0;completionProbeTarget=true;
 completionRecoveryResult=readingstats::CompletionRecoveryResult::Recovered;
 closedRestore.loop();assert(closedRestore.bookRestoreRecovery==ReadingStatsActivity::BookRestoreRecovery::Available);
 const int oldRecoveryCalls=completionRecoveryCalls;closedRestore.activateIndex(bookRestoreRow);closedRestore.loop();
 assert(completionRecoveryCalls==oldRecoveryCalls+1 && closedRestore.bookRestoreRecovery==ReadingStatsActivity::BookRestoreRecovery::None);
 closedRestore.bookRestoreRecovery=ReadingStatsActivity::BookRestoreRecovery::Available;
 completionRecoveryResult=readingstats::CompletionRecoveryResult::ManualReview;
 closedRestore.activateIndex(bookRestoreRow);closedRestore.loop();
 assert(completionRecoveryCalls==oldRecoveryCalls+2 && closedRestore.bookRestoreRecovery==ReadingStatsActivity::BookRestoreRecovery::ManualReview);
 closedRestore.bookRestoreRecovery=ReadingStatsActivity::BookRestoreRecovery::Checking;completionProbePolicy=1;
 closedRestore.loop();assert(closedRestore.bookRestoreRecovery==ReadingStatsActivity::BookRestoreRecovery::ManualReview);
 const int launchesBeforeManual=closedRestore.launched;closedRestore.activateIndex(bookRestoreRow);
 assert(closedRestore.launched==launchesBeforeManual && completionRecoveryCalls==oldRecoveryCalls+2);
 completionProbePolicy=0;
 retainedFailures=priorBookFailures;bookRestoreResult=statsbackup::BookRestoreResult::Restored;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
 completionProbeResult=readingstats::CompletionProbeResult::NoPending;
 completionRecoveryResult=readingstats::CompletionRecoveryResult::NoPending;
 completionRecoveryCalls=0;completionFinalizeCalls=0;completionProbeCalls=0;completionBookFixture={};
#endif
#endif
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 assert(book.tabCount()==3 && book.listCount()==29);
#else
 assert(book.tabCount()==3 && book.listCount()==28);
#endif
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
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION)
 ReadingStatsActivity completion(renderer,input,"The Book",stats,localFixture,3888,true,true,"cache","book",nullptr,true);
 completion.onEnter();
 int completionRow=-1;for(int i=0;i<completion.listCount();++i)if(completion.rowAt(i)==ReadingStatsActivity::Row::Completion)completionRow=i;
 assert(completionRow>=0);completion.activateIndex(completionRow);
 assert(completion.completionPending && completionCalls==0);
 assert(value(completion,ReadingStatsActivity::Row::Completion)=="STR_STATS_BACKUP_SAVING");
 completion.loop();
 assert(completionCalls==1 && !completion.completionPending && !completion.completionFailed && completion.book.isCompleted);
 assert(value(completion,ReadingStatsActivity::Row::Completion)=="STR_STATS_FINISHED");
 completionResult=readingstats::ClosedCompletionResult::RecoveryRequired;
 completion.activateIndex(completionRow);completion.loop();
 assert(completionCalls==2 && completion.completionFailed && !completion.saved);
 assert(value(completion,ReadingStatsActivity::Row::Completion)=="STR_RETRY");
 completionRecoveryResult=readingstats::CompletionRecoveryResult::Recovered;
 completion.activateIndex(completionRow);assert(completion.completionPending);completion.loop();
 assert(completionCalls==2 && completionRecoveryCalls==1 && !completion.completionFailed && completion.saved && !completion.book.isCompleted);
 completionResult=readingstats::ClosedCompletionResult::Applied;
 ReadingStatsActivity retryNew(renderer,input,"The Book",stats,localFixture,3888,true,true,"cache","book",nullptr,true);
 retryNew.onEnter();completionResult=readingstats::ClosedCompletionResult::Unavailable;
 retryNew.activateIndex(completionRow);retryNew.loop();assert(retryNew.completionFailed);
 completionResult=readingstats::ClosedCompletionResult::Applied;
 completionRecoveryResult=readingstats::CompletionRecoveryResult::NoPending;
 retryNew.activateIndex(completionRow);retryNew.loop();
 assert(completionRecoveryCalls==2 && completionCalls==4 && !retryNew.completionFailed && retryNew.book.isCompleted);
 BookReadingStats unavailableBook=stats;unavailableBook.writable=false;
 GlobalReadingStats unavailableGlobal=localFixture;unavailableGlobal.writable=false;
 ReadingStatsActivity reopened(renderer,input,"The Book",unavailableBook,unavailableGlobal,3888,true,true,"cache","book",nullptr,true);
 completionProbeResult=readingstats::CompletionProbeResult::Matching;completionProbeTarget=true;
 reopened.onEnter();assert(reopened.completionRecoveryChecking);reopened.loop();
 assert(completionProbeCalls==3 && reopened.completionFailed && reopened.completionDesired && !reopened.book.writable);
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 assert(reopened.bookRestoreRecovery==ReadingStatsActivity::BookRestoreRecovery::Available);
#endif
 assert(value(reopened,ReadingStatsActivity::Row::Completion)=="STR_RETRY");
 completionRecoveryResult=readingstats::CompletionRecoveryResult::Recovered;
 reopened.activateIndex(completionRow);assert(reopened.completionPending);reopened.loop();
 assert(completionRecoveryCalls==3 && !reopened.completionFailed && reopened.book.writable && reopened.book.isCompleted);
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 assert(reopened.bookRestoreRecovery==ReadingStatsActivity::BookRestoreRecovery::None);
#endif
 completionProbeResult=readingstats::CompletionProbeResult::NoPending;
 SETTINGS.removeReadBooksFromRecents=1;recentCompletionOk=false;
 ReadingStatsActivity recents(renderer,input,"The Book",stats,localFixture,3888,true,true,"cache","book",nullptr,true);
 recents.onEnter();recents.activateIndex(completionRow);recents.loop();
 assert(recents.completionFailed && recentCompletionCalls==1);
 recentCompletionOk=true;completionRecoveryResult=readingstats::CompletionRecoveryResult::NoPending;
 recents.activateIndex(completionRow);recents.loop();
 assert(!recents.completionFailed && recents.book.isCompleted && recentCompletionCalls==2);
 SETTINGS.removeReadBooksFromRecents=0;
 // A restarted retry retains the original move policy even if settings changed.
 completionProbeResult=readingstats::CompletionProbeResult::Matching;
 completionProbeTarget=true;completionProbePolicy=2;
 completionRecoveryResult=readingstats::CompletionRecoveryResult::Recovered;
 ReadingStatsActivity pendingMove(renderer,input,"The Book",unavailableBook,unavailableGlobal,3888,true,true,"cache","book.epub",nullptr,true);
 pendingMove.onEnter();pendingMove.loop();
 int finalizedBeforeMove=completionFinalizeCalls;
 pendingMove.activateIndex(completionRow);pendingMove.loop();
 assert(pendingMove.completionFailed && completionFinalizeCalls==finalizedBeforeMove);
 completionProbeResult=readingstats::CompletionProbeResult::NoPending;completionProbePolicy=0;
#if defined(CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE)
 SETTINGS.moveFinishedToReadFolder=1;
 assert(isInReadFolder("/read/Book.epub") && isInReadFolder("/READ/Book.epub"));
 assert(isInReadFolder("/ReAd/Sub/Book.epub"));
 assert(!isInReadFolder("/readme/Book.epub") && !isInReadFolder("/read/"));
 completionRecoveryResult=readingstats::CompletionRecoveryResult::NoPending;
 completionResult=readingstats::ClosedCompletionResult::Applied;
 ReadingStatsActivity alreadyRead(renderer,input,"The Book",stats,localFixture,3888,true,true,
                                  "cache:old","/READ/Book.epub",nullptr,true);
 alreadyRead.onEnter();alreadyRead.activateIndex(completionRow);alreadyRead.loop();
 assert(!alreadyRead.completionFailed && alreadyRead.trackingBookPath=="/READ/Book.epub");
 ReadingStatsActivity moveBook(renderer,input,"The Book",stats,localFixture,3888,true,true,
                               "cache:old","/Authors/Book.epub",nullptr,true);
 moveBook.onEnter();moveBook.activateIndex(completionRow);moveBook.loop();
 assert(!moveBook.completionFailed && moveBook.trackingBookPath=="/read/Book.epub");
 assert(moveBook.trackingCachePath=="cache:/read/Book.epub");
 assert(readingstats::loadedCompletionPath=="/read/Book.epub" &&
        readingstats::loadedCompletionCache=="cache:/read/Book.epub");
 ReadingStatsActivity exitMove(renderer,input,"The Book",stats,localFixture,3888,true,true,
                               "cache:old","/Authors/Another.epub",nullptr,true);
 exitMove.onEnter();exitMove.activateIndex(completionRow);
 const int writesBeforeExit=completionCalls, failuresBeforeExit=retainedFailures;
 exitMove.onExit();
 assert(completionCalls==writesBeforeExit && retainedFailures==failuresBeforeExit+1);
 SETTINGS.moveFinishedToReadFolder=0;
#endif
#endif
 book.activateIndex(startedRow);assert(book.launched==0);

 assert(value(book,ReadingStatsActivity::Row::Finished)=="-");
 assert(value(book,ReadingStatsActivity::Row::Morning)=="90 seconds");
 assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_OFF");
 int bookBackupRow=-1;for(int i=0;i<book.listCount();++i)if(book.rowAt(i)==ReadingStatsActivity::Row::BackupBook)bookBackupRow=i;
 assert(bookBackupRow>=0);int priorBackupCalls=backupCalls;backupResult=true;
 book.activateIndex(bookBackupRow);assert(backupCalls==priorBackupCalls);book.loop();
 assert(backupCalls==priorBackupCalls+1 && value(book,ReadingStatsActivity::Row::BackupBook)=="STR_STATS_BACKUP_SAVED");
 book.saved=false;book.activateIndex(bookBackupRow);assert(value(book,ReadingStatsActivity::Row::BackupBook)=="STR_STATS_BACKUP_UNAVAILABLE");book.saved=true;

 // A reader-menu request waits for the marker read and toggles exactly once.
 const int writesBeforeRequest=trackingWrites;
 trackingWriteOk=true;trackingFixture=booktracking::State::Disabled;
 ReadingStatsActivity requested(renderer,input,"B",stats,localFixture,100,true,true,"cache","book",nullptr);
 requested.requestTrackingToggle();requested.onEnter();
 requested.loop();assert(trackingWrites==writesBeforeRequest && requested.pendingTrackingToggle);
 requested.loop();assert(!requested.pendingTrackingToggle && trackingWrites==writesBeforeRequest);
 assert(requested.rowAt(requested.ringPos()-1)==ReadingStatsActivity::Row::Tracking);
 requested.loop();assert(trackingWrites==writesBeforeRequest+1 && trackingFixture==booktracking::State::Enabled);
 requested.loop();assert(trackingWrites==writesBeforeRequest+1);
 requested.requestTrackingToggle();requested.loop();requested.loop();
 assert(trackingWrites==writesBeforeRequest+2 && trackingFixture==booktracking::State::Disabled);
 trackingWriteOk=false;requested.requestTrackingToggle();requested.loop();requested.loop();
 assert(trackingWrites==writesBeforeRequest+3 && trackingFixture==booktracking::State::Disabled);
 assert(value(requested,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_CHECK_AGAIN");
 requested.loop();assert(trackingWrites==writesBeforeRequest+3);
 trackingFixture=booktracking::State::Unknown;
 ReadingStatsActivity unknown(renderer,input,"B",stats,localFixture,100,true,true,"cache","book",nullptr);
 unknown.requestTrackingToggle();unknown.onEnter();unknown.loop();unknown.loop();
 assert(!unknown.pendingTrackingToggle && trackingWrites==writesBeforeRequest+3);
 assert(value(unknown,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_CHECK_AGAIN");
 device.requestTrackingToggle();assert(!device.pendingTrackingToggle);
 trackingFixture=booktracking::State::Disabled;trackingWrites=0;trackingWriteOk=true;
 int trackingRow=-1;for(int i=0;i<book.listCount();++i)if(book.rowAt(i)==ReadingStatsActivity::Row::Tracking)trackingRow=i;
 assert(trackingRow>=0);book.activateIndex(trackingRow);assert(trackingWrites==0);book.loop();
 assert(trackingWrites==1 && value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_ON");
 trackingWriteOk=false;book.activateIndex(trackingRow);book.loop();
 assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_CHECK_AGAIN");
 book.activateIndex(trackingRow);book.loop();assert(value(book,ReadingStatsActivity::Row::Tracking)=="STR_STATS_TRACKING_NEXT_ON");
 for(int tab=0;tab<book.tabCount();++tab){
  book.onTabAction(tab);
  for(int row=0;row<book.listCount();++row){freeink::ui::ListItem item;book.provideRow(&book,row,item);assert(item.label);assert(item.value || (tab==0 && row==0) || book.rowAt(row)==ReadingStatsActivity::Row::Charts || book.rowAt(row)==ReadingStatsActivity::Row::BackupDevice || book.rowAt(row)==ReadingStatsActivity::Row::BackupBook
#if defined(CROSSPOINT_EXPERIMENTAL_STATS_RESTORE)
 || book.rowAt(row)==ReadingStatsActivity::Row::RestoreDevice || book.rowAt(row)==ReadingStatsActivity::Row::RestoreBook || book.rowAt(row)==ReadingStatsActivity::Row::ResetDevice
#endif
 );}
  assert(book.rowAt(book.listCount()-1)==ReadingStatsActivity::Row::Sunday);
  UiScreen ui;book.buildScreen(ui);book.drawFooter();
 }
 book.onTabAction(0);book.saved=false;
 int chartsRow=-1;for(int i=0;i<book.listCount();++i)if(book.rowAt(i)==ReadingStatsActivity::Row::Charts)chartsRow=i;
 book.ring=chartsRow+1;input.released=static_cast<int>(MappedInputManager::Button::Confirm);book.handleButtons();
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
    command = ["c++", "-std=c++20", *(["-DCROSSPOINT_EXPERIMENTAL_COMPLETION"] if os.getenv("TEST_EXPERIMENTAL_COMPLETION") else []), *(["-DCROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE"] if os.getenv("TEST_EXPERIMENTAL_COMPLETION_MOVE") else []), *(["-DCROSSPOINT_EXPERIMENTAL_STATS_RESTORE"] if os.getenv("TEST_EXPERIMENTAL_STATS_RESTORE") else []), "-I", str(path), "-I", str(root / "src"), "-I", str(root / "lib/I18n"),
               str(source), str(root / "src/activities/reader/ReadingStatsDateEdit.cpp"), str(path / "test.cpp"), "-o", str(path / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(path / "test")], check=True)
print("Statistics tab, row, unknown-data and navigation checks passed")
