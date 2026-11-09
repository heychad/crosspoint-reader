"""Exercise extracted production settings methods with a fault-injecting host storage."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/CrossPointSettings.cpp').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    level = 0
    for pos in range(opening, len(source)):
        level += (source[pos] == '{') - (source[pos] == '}')
        if not level:
            return source[start:pos + 1]
    raise ValueError(signature)

methods = '\n'.join(extract(signature) for signature in
                    ('bool CrossPointSettings::saveSpecializedToFile() const', 'bool CrossPointSettings::loadSpecializedFromFile()'))
prefix = r'''
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Memory.h>
#include <SettingsRecoveryProbe.h>
#include <SettingsSafeWrite.h>
#include <StatsSaveNotice.h>
#include <PersistableStore.h>
#include <cassert>
#include <cstring>
#include <mutex>
#include <string>
#define LOG_ERR(...) ((void)0)
uint32_t noticeWord = 0;
StatsSaveNotice& settingsSaveNotice() { static StatsSaveNotice notice(noticeWord); return notice; }
class CrossPointSettings : public PersistableStore<CrossPointSettings> {
  friend class PersistableStore<CrossPointSettings>;
  bool saveSpecializedToFile() const;
  bool loadSpecializedFromFile();
 public:
  int font = 10;
  static const char* getFilePath() { return "/.crosspoint/settings.json"; }
  void toJson(JsonDocument& doc) const { doc["fontSize"] = font; }
  bool fromJson(JsonVariantConst doc) {
    font = doc["fontSize"] | 10;
    if (doc["migrate"] | false) resaveRequested = true;
    return true;
  }
};
int legacySaves=0;
bool PersistableStoreBase::writeDocToFile(const char*, const JsonDocument&){++legacySaves;return true;}
bool PersistableStoreBase::readDocFromFile(const char*, JsonDocument& doc, size_t){doc["value"]=42;return true;}
class OrdinaryStore : public PersistableStore<OrdinaryStore> {
 public:
  int value=0;
  static const char* getFilePath(){return "/ordinary.json";}
  void toJson(JsonDocument& doc) const {doc["value"]=value;}
  bool fromJson(JsonVariantConst doc){value=doc["value"]|0;return true;}
};
'''
body = r'''
void put(const char* path,const char* text){HalFile f;assert(Storage.openFileForWrite("TEST",path,f));assert(f.write(text,strlen(text))==strlen(text));assert(f.close());}
int main(){
 const char* primary="/.crosspoint/settings.json", *backup="/.crosspoint/settings.json.bak", *temp="/.crosspoint/settings.json.tmp";
 Storage.reset(); CrossPointSettings s; s.font=14; assert(s.saveToFile()); s.font=16;assert(s.saveToFile());
 auto& base=static_cast<PersistableStore<CrossPointSettings>&>(s);s.font=18;assert(base.saveToFile()&&legacySaves==0);s.font=16;assert(base.saveToFile()&&legacySaves==0);
 CrossPointSettings loaded;assert(loaded.loadFromFile()&&loaded.font==16);
 loaded.font=10;auto& loadBase=static_cast<PersistableStore<CrossPointSettings>&>(loaded);assert(loadBase.loadFromFile()&&loaded.font==16&&legacySaves==0);
 Storage.reset();put(backup,"{\"fontSize\":14,\"migrate\":true}");put(temp,"{broken");
 CrossPointSettings recovered;assert(recovered.loadFromFile()&&recovered.font==14);assert(!recovered.saveToFile());assert(Storage.exists(backup)&&Storage.exists(temp)&&!Storage.exists(primary));
 assert(settingsSaveNotice().pending()!=0); auto seen=settingsSaveNotice().pending();assert(settingsSaveNotice().acknowledge(seen));
 Storage.reset();put(primary,"{\"fontSize\":15}");put(backup,"{\"fontSize\":14}");
 CrossPointSettings current;assert(current.loadFromFile()&&current.font==15);assert(current.saveToFile());
 assert(Storage.exists("/.crosspoint/settings.recovered-01.bak"));
 Storage.failNextReadFor(primary);CrossPointSettings unavailable;assert(!unavailable.loadFromFile());assert(Storage.exists("/.crosspoint/settings.recovered-01.bak"));
 Storage.reset();put(primary,"{\"fontSize\":15}");Storage.failCheckedOpenAfter(1);CrossPointSettings noHandle;assert(!noHandle.loadFromFile());assert(Storage.exists(primary));
 Storage.reset();put(primary,"{\"fontSize\":17,\"migrate\":true}");CrossPointSettings migrated;assert(migrated.loadFromFile()&&migrated.font==17);assert(!Storage.exists(backup)&&!Storage.exists(temp));
 OrdinaryStore ordinary;assert(ordinary.loadFromFile()&&ordinary.value==42);assert(ordinary.saveToFile()&&legacySaves==1);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'fixture.cpp').write_text(prefix + methods + body)
    command = ['c++', '-std=c++20', '-O1', '-fsanitize=address', '-fno-omit-frame-pointer',
               '-I', str(ROOT / 'test/settings_recovery_plan/stubs'),
               '-I', str(ROOT / 'test/book_reading_stats_atomic/stubs'),
               '-I', str(ROOT / 'lib/Serialization'),
               '-I', str(ROOT / 'src/activities/reader'),
               '-I', str(ROOT / '.cache/font-shortcut-host/_deps/arduinojson-src/src'),
               str(path / 'fixture.cpp'), str(ROOT / 'lib/Serialization/SettingsSafeWrite.cpp'),
               str(ROOT / 'lib/Serialization/SettingsRecoveryProbe.cpp'), '-o', str(path / 'fixture')]
    subprocess.run(command, check=True)
    subprocess.run([str(path / 'fixture')], check=True)
    print('Settings load/save integration paths passed under AddressSanitizer')
