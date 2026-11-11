"""Exercise the real writer with isolated sleep and book-move artifacts."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
program = r'''
#include <HalStorage.h>
#include <MoveReferenceSafeWrite.h>
#include <cassert>
#include <cstring>
#include <string>
constexpr char primary[]="/.crosspoint/state.json";
constexpr char sleepTemp[]="/.crosspoint/state.json.sleep-tmp";
constexpr char sleepBackup[]="/.crosspoint/state.json.sleep-bak";
constexpr char moveTemp[]="/.crosspoint/state.json.tmp";
constexpr char next[]="{\"openEpubPath\":\"/new.epub\"}";
void put(const char* path,const char* text) {
 HalFile f; assert(Storage.openFileForWrite("TEST",path,f));
 assert(f.write(text,strlen(text))==strlen(text)); assert(f.close());
}
bool validState(const char* path) {
 HalFile f; if (!Storage.openFileForRead("TEST",path,f)) return false;
 char buffer[80]{}; const auto n=f.read(buffer,sizeof(buffer)-1); f.close();
 return n>1 && buffer[0]=='{' && buffer[n-1]=='}';
}
int main() {
 Storage.reset(); put(primary,"{\"openEpubPath\":\"/old.epub\"}"); put(moveTemp,"move");
 assert(!movereference::writeSleepPreferences(next,sizeof(next)-1));
 assert(Storage.exists(moveTemp) && !Storage.exists(sleepTemp));
 Storage.reset(); put(primary,"{\"openEpubPath\":\"/old.epub\"}");
 Storage.failNextRenameFrom(sleepTemp);
 assert(!movereference::writeSleepPreferences(next,sizeof(next)-1));
 assert(Storage.exists(sleepTemp) && Storage.exists(sleepBackup));
 assert(!movereference::write(primary,next,sizeof(next)-1));
 assert(Storage.exists(sleepTemp) && Storage.exists(sleepBackup));
 Storage.reset(); put(primary,"{\"openEpubPath\":\"/old.epub\"}");
 assert(movereference::writeSleepPreferences(next,sizeof(next)-1));
 assert(Storage.exists("/.crosspoint/state.json.sleep-history-0000"));
 assert(!Storage.exists(sleepTemp) && !Storage.exists(sleepBackup));
 Storage.reset(); put(primary,"{\"openEpubPath\":\"/old.epub\"}"); put(sleepTemp,"partial");
 assert(movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && !Storage.exists(sleepTemp));
 assert(Storage.exists("/.crosspoint/state.sleep-recovered-0000"));
 Storage.reset(); put(sleepBackup,"{\"openEpubPath\":\"/old.epub\"}"); put(sleepTemp,"partial");
 assert(movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && !Storage.exists(sleepBackup) && !Storage.exists(sleepTemp));
 Storage.reset(); put(primary,"broken"); put(sleepBackup,"{\"openEpubPath\":\"/old.epub\"}");
 assert(!movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && Storage.exists(sleepBackup));
 Storage.reset(); put(primary,"{}"); put(sleepTemp,"partial"); put(moveTemp,"move");
 assert(!movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && Storage.exists(sleepTemp) && Storage.exists(moveTemp));
 Storage.reset(); put(primary,"{}"); put(sleepTemp,"partial");
 Storage.failNextRenameFrom(sleepTemp);
 assert(!movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && Storage.exists(sleepTemp));
 assert(movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && !Storage.exists(sleepTemp));
 Storage.reset(); put(primary,"{}"); put(sleepTemp,"partial"); Storage.failSettingsIdentity();
 assert(!movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && Storage.exists(sleepTemp));
 Storage.reset(); put(primary,"{}"); put(sleepTemp,"partial"); Storage.mkdir("/.completion-journal");
 assert(!movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && Storage.exists(sleepTemp));
 Storage.reset(); put(sleepBackup,"not-json"); put(sleepTemp,"partial");
 assert(!movereference::recoverSleepPreferences(validState));
 assert(!Storage.exists(primary) && Storage.exists(sleepBackup) && Storage.exists(sleepTemp));
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-DCROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY=1',
                    '-I', str(root / 'test/book_reading_stats_atomic/stubs'),
                    '-I', str(root / 'lib/Serialization'), str(path / 'test.cpp'),
                    str(root / 'lib/Serialization/MoveReferenceSafeWrite.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Sleep and move state writer isolation passed')
