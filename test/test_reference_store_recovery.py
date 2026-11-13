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
constexpr char OLD[] = "/Authors/Example.epub";
constexpr char NEXT[] = "/read/Example.epub";
std::string read(const char* path) {
  std::string value;
  assert(Storage.readFileToString("TEST",path,65536,value));
  return value;
}
void seed() {
  Storage.reset();
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
int main() {
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
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(ROOT / 'src'), '-I', str(ROOT / 'test/book_reader_settings/stubs'),
                    '-I', str(ROOT / 'lib/Serialization'),
                    '-I', str(ROOT / 'test/book_reading_stats_atomic/stubs'),
                    '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(source), *map(str, sources), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Native state/Recents stores recover a published move after retry or unchanged reload; conflicts stay intact')
