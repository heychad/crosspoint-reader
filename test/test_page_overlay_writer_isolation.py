"""Exercise the real writer with isolated sleep and book-move artifacts."""
from pathlib import Path
import os
import shlex
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
bool readText(const char* path,std::string& text) {
 HalFile f;if(!Storage.openFileForRead("TEST",path,f))return false;
 char buffer[80];const auto n=f.read(buffer,sizeof(buffer));
 if(n<0||!f.close())return false;text.assign(buffer,static_cast<size_t>(n));return true;
}
bool validState(const char* path) {
 HalFile f; if (!Storage.openFileForRead("TEST",path,f)) return false;
 char buffer[80]{}; const auto n=f.read(buffer,sizeof(buffer)-1); f.close();
 return n>1 && buffer[0]=='{' && buffer[n-1]=='}';
}
int main() {
 for(size_t cut=0;cut<sizeof(next)-1;++cut){
  Storage.reset();put(primary,"{\"openEpubPath\":\"/old.epub\"}");
  Storage.failNextWriteFor(sleepTemp,cut);
  assert(!movereference::writeSleepPreferences(next,sizeof(next)-1));
  std::string preserved;
  assert(readText(primary,preserved));
  assert(preserved=="{\"openEpubPath\":\"/old.epub\"}"&&Storage.exists(sleepTemp));
  assert(movereference::recoverSleepPreferences(validState));
  assert(movereference::writeSleepPreferences(next,sizeof(next)-1));
  assert(readText(primary,preserved)&&preserved==next);
 }
 for(int failure=0;failure<2;++failure){
  Storage.reset();put(primary,"{\"openEpubPath\":\"/old.epub\"}");
  if(failure==0)Storage.failNextNewSyncFor(sleepTemp);else Storage.failNextNewCloseFor(sleepTemp);
  assert(!movereference::writeSleepPreferences(next,sizeof(next)-1));
  std::string preserved;
  assert(readText(primary,preserved));
  assert(preserved=="{\"openEpubPath\":\"/old.epub\"}");
  assert(movereference::recoverSleepPreferences(validState));
  assert(movereference::writeSleepPreferences(next,sizeof(next)-1));
 }
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
 assert(!Storage.exists("/.crosspoint/state.json.sleep-history-0000"));
 assert(!Storage.exists(sleepTemp) && !Storage.exists(sleepBackup));
 for(unsigned n=0;n<128;++n){
  const auto value="{\"readerActivityLoadCount\":"+std::to_string(n)+"}";
  assert(movereference::writeSleepPreferences(value.data(),value.size()));
  std::string actual;assert(readText(primary,actual) && actual==value);
  assert(!Storage.exists(sleepTemp) && !Storage.exists(sleepBackup));
  assert(!Storage.exists("/.crosspoint/state.json.sleep-history-0000"));
 }
 // A full historical namespace must not prevent ordinary state saves.
 for(unsigned n=0;n<10000;++n){
  char path[80];snprintf(path,sizeof(path),"/.crosspoint/state.json.sleep-history-%04u",n);put(path,"history");
 }
 assert(movereference::writeSleepPreferences(next,sizeof(next)-1));
 std::string history;
 assert(readText("/.crosspoint/state.json.sleep-history-0000",history) && history=="history");
 assert(readText("/.crosspoint/state.json.sleep-history-9999",history) && history=="history");
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
 // The state owner checks journal policy while holding the storage lock.
 assert(movereference::recoverSleepPreferences(validState));
 assert(Storage.exists(primary) && !Storage.exists(sleepTemp) && Storage.exists("/.completion-journal"));
 Storage.reset(); put(sleepBackup,"not-json"); put(sleepTemp,"partial");
 assert(!movereference::recoverSleepPreferences(validState));
 assert(!Storage.exists(primary) && Storage.exists(sleepBackup) && Storage.exists(sleepTemp));
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-DCROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY=1',
                    *shlex.split(os.environ.get('CXXFLAGS', '')),
                    '-I', str(root / 'test/book_reading_stats_atomic/stubs'),
                    '-I', str(root / 'lib/Serialization'), str(path / 'test.cpp'),
                    str(root / 'lib/Serialization/MoveReferenceSafeWrite.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Sleep and move state writer isolation passed')
