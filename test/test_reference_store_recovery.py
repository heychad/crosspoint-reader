"""Run the native state/Recents stores and move writer against the host storage model."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PROGRAM = r'''
#include <cassert>
#include <string>
#include <HalStorage.h>
#include "RecentBooksStore.h"
#include "CrossPointState.h"
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
#include <Memory.h>
#include "activities/reader/CompletionJournalIO.h"
#include "activities/reader/StatsSaveNotice.h"
namespace cj = readingstats::completionjournal;
cj::ScanResult journalState=cj::ScanResult::Empty;
unsigned journalScans=0;
// The real access gate consumes each scanner result; scanner/retirement I/O
// is covered separately by the completion journal suites.
cj::ScanResult cj::scanRecords(Record&,size_t&,unsigned&) {++journalScans;return journalState;}
StatsSaveNotice& stateSaveNotice() {static uint32_t word=0;static StatsSaveNotice notice(word);return notice;}
#endif
constexpr char OLD[] = "/Authors/Example.epub";
constexpr char NEXT[] = "/read/Example.epub";
std::string read(const char* path) {
  std::string value;
  assert(Storage.readFileToString("TEST",path,65536,value));
  return value;
}
void seed() {
  Storage.reset();
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  stateSaveNotice().initialize(false);
#endif
  JsonDocument state, recent;
  state["openEpubPath"]=OLD;state["favoriteSleepImagePath"]="/favorite.bmp";
  state["readerActivityLoadCount"]=7;
  assert(APP_STATE.fromJson(state.as<JsonVariantConst>()));
  assert(APP_STATE.saveToFile());
  assert(!deserializeJson(recent,R"({"books":[
    {"path":"/Authors/Example.epub","title":"Example","author":"Writer","coverBmpPath":"/cache/old/cover.bmp"},
    {"path":"/Other.epub","title":"Other","author":"Else","coverBmpPath":"/cache/other.bmp"}]})"));
  assert(RECENT_BOOKS.fromJson(recent.as<JsonVariantConst>()));
  assert(RECENT_BOOKS.saveToFile());
}
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
void checkSleepRecovery() {
  constexpr char primary[]="/.crosspoint/state.json";
  constexpr char temp[]="/.crosspoint/state.json.sleep-tmp";
  constexpr char journal[]="/.completion-journal";
  for(const auto state : {cj::ScanResult::Empty,cj::ScanResult::PreparedOnly,cj::ScanResult::Candidate,
                         cj::ScanResult::Conflict,cj::ScanResult::Damaged,cj::ScanResult::Unavailable}) {
    seed();journalState=state;journalScans=0;
    Storage.mkdir(journal);
    assert(Storage.writeFile(temp,"partial"));
    const auto before=read(primary);
    APP_STATE.favoriteSleepImagePath="/new.bmp";
    const bool allowed=state==cj::ScanResult::Empty || state==cj::ScanResult::PreparedOnly;
    assert(APP_STATE.recoverSleepStateForPageOverlay()==allowed && journalScans==1);
    assert(Storage.exists(journal));
    if(allowed) {
      JsonDocument doc;assert(!deserializeJson(doc,read(primary)));
      assert(doc["openEpubPath"]==OLD && doc["favoriteSleepImagePath"]=="/new.bmp");
      assert(doc["readerActivityLoadCount"]==7);
      assert(!Storage.exists(temp));
      assert(read("/.crosspoint/state.sleep-recovered-0000")=="partial");
      assert(read("/.crosspoint/state.json.sleep-history-0000")==before);
    } else {
      assert(read(primary)==before && read(temp)=="partial");
      assert(!Storage.exists("/.crosspoint/state.sleep-recovered-0000"));
      assert(stateSaveNotice().pending());
    }
  }
  // Failure before the guarded operation must preserve every recovery artifact.
  for(int failure=0;failure<3;++failure) {
    seed();Storage.mkdir(journal);assert(Storage.writeFile(temp,"partial"));
    const auto before=read(primary);journalState=cj::ScanResult::Empty;
    if(failure==0)Storage.failReadsFor(journal);
    if(failure==1)Storage.failNextStorageLock();
    if(failure==2)failNextTestAllocation=true;
    assert(!APP_STATE.recoverSleepStateForPageOverlay());
    assert(read(primary)==before && read(temp)=="partial");
    assert(stateSaveNotice().pending());
  }
  // No leftover sleep artifact still allows retrying the ordinary state save.
  seed();Storage.mkdir(journal);journalState=cj::ScanResult::Empty;
  APP_STATE.favoriteSleepImagePath="/new.bmp";
  assert(APP_STATE.recoverSleepStateForPageOverlay());
  JsonDocument doc;assert(!deserializeJson(doc,read(primary)));
  assert(doc["openEpubPath"]==OLD && doc["favoriteSleepImagePath"]=="/new.bmp");
  // A clean journal does not authorize resolving a move's state artifacts.
  seed();Storage.mkdir(journal);assert(Storage.writeFile("/.crosspoint/state.json.tmp","move"));
  const auto before=read(primary);
  assert(!APP_STATE.recoverSleepStateForPageOverlay());
  assert(read(primary)==before && read("/.crosspoint/state.json.tmp")=="move");
}
#endif
int main() {
#if defined(CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY)
  checkSleepRecovery();
#endif
  for (bool reboot : {false,true}) {
    seed();
    const auto stateBefore=read("/.crosspoint/state.json");
    Storage.failNextRenameFrom("/.crosspoint/state.json.bak");
    assert(!APP_STATE.updateOpenEpubPathPersisted(OLD,NEXT));
    assert(APP_STATE.openEpubPath==OLD);
    JsonDocument published;assert(!deserializeJson(published,read("/.crosspoint/state.json")));
    assert(published["openEpubPath"]==NEXT);
    if(reboot) assert(APP_STATE.loadFromFile());
    assert(APP_STATE.updateOpenEpubPathPersisted(OLD,NEXT));
    assert(APP_STATE.openEpubPath==NEXT && APP_STATE.favoriteSleepImagePath=="/favorite.bmp");
    assert(APP_STATE.readerActivityLoadCount==7);
    assert(!Storage.exists("/.crosspoint/state.json.bak"));
    assert(read("/.crosspoint/state.json.move-history-0000")==stateBefore);
    const auto recentBefore=read("/.crosspoint/recent.json");
    Storage.failNextRenameFrom("/.crosspoint/recent.json.bak");
    assert(!RECENT_BOOKS.updatePathPersisted(OLD,NEXT,"/cache/old","/cache/old"));
    assert(RECENT_BOOKS.getBooks()[0].path==OLD);
    if(reboot) assert(RECENT_BOOKS.loadFromFile());
    assert(RECENT_BOOKS.updatePathPersisted(OLD,NEXT,"/cache/old","/cache/old"));
    assert(RECENT_BOOKS.getCount()==2 && RECENT_BOOKS.getBooks()[0].path==NEXT);
    assert(RECENT_BOOKS.getBooks()[0].coverBmpPath=="/cache/old/cover.bmp");
    assert(RECENT_BOOKS.getBooks()[1].path=="/Other.epub");
    assert(!Storage.exists("/.crosspoint/recent.json.bak"));
    assert(read("/.crosspoint/recent.json.move-history-0000")==recentBefore);
    seed();
    Storage.failNextRenameFrom("/.crosspoint/recent.json.bak");
    assert(!RECENT_BOOKS.removeByPathPersisted(OLD));
    if(reboot) assert(RECENT_BOOKS.loadFromFile());
    assert(RECENT_BOOKS.removeByPathPersisted(OLD));
    assert(RECENT_BOOKS.getCount()==1 && RECENT_BOOKS.getBooks()[0].path=="/Other.epub");
    assert(!Storage.exists("/.crosspoint/recent.json.bak"));
  }
  // A changed in-memory snapshot is not permission to replace the published primary.
  seed();
  Storage.failNextRenameFrom("/.crosspoint/state.json.bak");
  assert(!APP_STATE.updateOpenEpubPathPersisted(OLD,NEXT));
  assert(APP_STATE.loadFromFile());
  APP_STATE.readerActivityLoadCount=8;
  const auto primary=read("/.crosspoint/state.json"), backup=read("/.crosspoint/state.json.bak");
  assert(!APP_STATE.updateOpenEpubPathPersisted(OLD,NEXT));
  assert(read("/.crosspoint/state.json")==primary && read("/.crosspoint/state.json.bak")==backup);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    directory = Path(temporary)
    source, binary = directory / 'test.cpp', directory / 'test'
    source.write_text(PROGRAM)
    sources = [ROOT / name for name in (
        'src/CrossPointState.cpp', 'src/RecentBooksStore.cpp',
        'lib/Serialization/PersistableStore.cpp', 'lib/Serialization/MoveReferenceSafeWrite.cpp')]
    for overlay in (False, True):
        overlay_flags = ['-DCROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY=1'] if overlay else []
        overlay_sources = [ROOT / 'src/activities/reader/StatsAccessGate.cpp'] if overlay else []
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *overlay_flags,
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'), '-I', str(ROOT / 'test/book_reader_settings/stubs'),
                    '-I', str(ROOT / 'lib/Serialization'),
                    '-I', str(ROOT / 'test/book_reading_stats_atomic/stubs'),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(source), *map(str, sources + overlay_sources), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
print('Native reference recovery and sleep recovery gating passed with Page Overlay off/on')
