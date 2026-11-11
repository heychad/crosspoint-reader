"""Exercise the reader adapter with the actual Recents removal method."""
from pathlib import Path
import subprocess
import tempfile
import os

root = Path(__file__).resolve().parents[1]
recents = (root / 'src/RecentBooksStore.cpp').read_text()
start = recents.index('bool RecentBooksStore::removeByPathPersisted(')
opening = recents.index('{', start)
depth = 0
for index in range(opening, len(recents)):
    if recents[index] == '{':
        depth += 1
    elif recents[index] == '}':
        depth -= 1
        if depth == 0:
            method = recents[start:index + 1]
            break
else:
    raise RuntimeError('Recents method not found')
adapter = (root / 'src/activities/reader/ReaderCompletion.cpp').read_text()
adapter = adapter[adapter.index('namespace {'):adapter.index('void ReaderActivity::handleCompletionAttempt(')]
code = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>
#include "util/ReadFolder.h"
#define LOG_ERR(...) ((void)0)
struct RecentBook {std::string path, title;};
class RecentBooksStore {
public:
 std::vector<RecentBook> recentBooks; bool success=true; int saves=0;
 std::mutex storeMutex;
 bool saveForCompletionLocked(){++saves;return success;}
 bool removeByPathPersisted(const std::string&);
};
RecentBooksStore store;
#define RECENT_BOOKS store
''' + method + r'''
struct Settings {bool moveFinishedToReadFolder=false, removeReadBooksFromRecents=false;} SETTINGS;
namespace FsHelpers {bool hasEpubExtension(const std::string& path){return path.ends_with(".epub");}}
struct ReadingStatsDate {int year=0;};
struct ReadingStatsDateTime {ReadingStatsDate date;};
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& now){now.date.year=2026;return true;}
bool renderHeld=false;
struct RenderLock {RenderLock(){assert(!renderHeld);renderHeld=true;} ~RenderLock(){renderHeld=false;}};
class ReaderStats {
public:
 enum class CompletionResult {Applied, Unavailable, RecoveryRequired};
 using Remove = bool (*)(const std::string&,void*);
 int edits=0; bool requestedRemoval=false, requestedDeferred=false; int date=0;
 CompletionResult editCompletionStats(const std::string&,const std::string& path,bool,
                                      const ReadingStatsDate& today,bool remove,Remove callback,void*,bool defer){
  assert(renderHeld);++edits;requestedRemoval=remove;requestedDeferred=defer;date=today.year;
  if(remove && !callback(path,nullptr))return CompletionResult::RecoveryRequired;
  return CompletionResult::Applied;
 }
 CompletionResult recoverCompletionStats(const std::string&,const std::string& path,Remove callback){
  assert(renderHeld);return callback(path,nullptr)?CompletionResult::Applied:CompletionResult::RecoveryRequired;
 }
};
class ReaderActivity {
public:
 std::string bookPath="/book.epub", cache="/cache";
 std::atomic<bool> pageRendered{true}; ReaderStats readingStats;
 const std::string& getBookCachePath()const{return cache;}
 ReaderStats::CompletionResult setBookCompleted(bool);
 ReaderStats::CompletionResult recoverBookCompletion();
};
''' + adapter + r'''
int main(){
 ReaderActivity reader;
 reader.pageRendered=false;
 assert(reader.setBookCompleted(true)==ReaderStats::CompletionResult::Unavailable);
 assert(reader.readingStats.edits==0 && !renderHeld);
 reader.pageRendered=true;SETTINGS.moveFinishedToReadFolder=true;
#if defined(CROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION)
 reader.bookPath="/book.xtc";
 assert(reader.setBookCompleted(true)==ReaderStats::CompletionResult::Unavailable);
 reader.bookPath="/book.epub";
 assert(reader.setBookCompleted(true)==ReaderStats::CompletionResult::Applied);
 assert(reader.readingStats.requestedDeferred && reader.readingStats.edits==1);
 reader.readingStats.edits=0;
#else
 assert(reader.setBookCompleted(true)==ReaderStats::CompletionResult::Unavailable);
 assert(reader.readingStats.edits==0 && !renderHeld);
#endif
 reader.bookPath="/READ/book.epub";
 assert(reader.setBookCompleted(true)==ReaderStats::CompletionResult::Applied);
 assert(reader.readingStats.edits==1 && !reader.readingStats.requestedDeferred && !renderHeld);
 reader.bookPath="/book.epub";
 SETTINGS.moveFinishedToReadFolder=false;SETTINGS.removeReadBooksFromRecents=true;
 store.recentBooks={{"/book.epub","Book"},{"/other.epub","Other"}};
 store.success=false;
 assert(reader.setBookCompleted(true)==ReaderStats::CompletionResult::RecoveryRequired);
 assert(reader.readingStats.requestedRemoval && reader.readingStats.date==2026);
 assert(store.recentBooks.size()==2 && store.recentBooks[0].title=="Book");
 store.success=true;
 assert(reader.recoverBookCompletion()==ReaderStats::CompletionResult::Applied);
 assert(store.recentBooks.size()==1 && store.recentBooks[0].path=="/other.epub");
 assert(reader.setBookCompleted(false)==ReaderStats::CompletionResult::Applied);
 assert(!reader.readingStats.requestedRemoval && !renderHeld);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    flags = ['-DCROSSPOINT_EXPERIMENTAL_DEFERRED_AUTOMATION'] if os.environ.get('TEST_DEFERRED_AUTOMATION') else []
    subprocess.run(['c++', '-std=c++20', *flags, '-I', str(root / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Reader completion adapter checks passed')
