"""Exercise the actual sleep-time automatic statistics backup decision."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/main.cpp').read_text()
sleep = source.index('  activityManager.goToSleep(fromTimeout);')
start = source.index('#if defined(CROSSPOINT_EXPERIMENTAL_STATS_BACKUP)', sleep)
end = source.index('#endif', start)
assert sleep < start < end < source.index('  Storage.prepareForDeepSleep();', end)
block = source[start:end + len('#endif')]
code = r'''
#include <cassert>
#define CROSSPOINT_EXPERIMENTAL_STATS_BACKUP 1
struct Date {int value=0;};
struct ReadingStatsDateTime {Date date;};
struct Settings {bool trackReadingStats=true,autoBackupStats=true;} SETTINGS;
bool clockOk=true;int calls=0,errors=0;bool fail=false;
bool getCurrentLocalReadingStatsDateTime(ReadingStatsDateTime& now){now.date.value=10;return clockOk;}
namespace statsbackup {
enum class AutomaticBackupResult {Saved,AlreadyPresent,Unavailable};
AutomaticBackupResult createDailyDeviceBackup(Date d){assert(d.value==10);++calls;return fail?AutomaticBackupResult::Unavailable:AutomaticBackupResult::Saved;}
}
#define LOG_ERR(...) (++errors)
void run(){
''' + block + r'''
}
int main(){
 SETTINGS.trackReadingStats=false;run();assert(calls==0);
 SETTINGS.trackReadingStats=true;SETTINGS.autoBackupStats=false;run();assert(calls==0);
 SETTINGS.autoBackupStats=true;clockOk=false;run();assert(calls==0);
 clockOk=true;run();assert(calls==1 && errors==0);
 fail=true;run();assert(calls==2 && errors==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Sleep backup respects tracking, backup preference, clock availability, and failure reporting')
