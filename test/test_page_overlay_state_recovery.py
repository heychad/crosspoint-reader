"""Exercise production state save fencing and read-only backup fallback."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/CrossPointState.cpp').read_text()
start = source.index('bool CrossPointState::saveSpecializedToFile() const {')
end = source.index('\n#endif', start)
methods = source[start:end]
program = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <map>
#include <mutex>
#include <string>
struct HalStorage { enum class PathStatus { Missing, Present, Failed }; };
struct Notice { unsigned value=0; unsigned pending(){return value;} void recordFailure(){++value;} } notice;
Notice& stateSaveNotice(){return notice;}
namespace movereference { bool recoverSleepPreferences(bool (*)(const char*)){return true;} }
struct MockStorage {
 std::map<std::string,std::string> files;
 bool lookupFailure=false;
 HalStorage::PathStatus lookupPath(const char* path) {
   if (lookupFailure) return HalStorage::PathStatus::Failed;
   return files.count(path) ? HalStorage::PathStatus::Present : HalStorage::PathStatus::Missing;
 }
 bool withStorageLock(bool (*fn)(void*),void* arg) {return fn(arg);}
} Storage;
struct PersistableStoreBase {
 static bool writeDocToFile(const char* path,const JsonDocument& doc) {
   std::string result; serializeJson(doc,result); Storage.files[path]=result; return true;
 }
 static bool readDocFromFile(const char* path,JsonDocument& doc,size_t max) {
   auto it=Storage.files.find(path);
   return it!=Storage.files.end() && it->second.size()<=max && !deserializeJson(doc,it->second);
 }
 static bool writeStateSleepPreferences(const JsonDocument&) {return Storage.lookupFailure == false;}
};
struct CrossPointState : PersistableStoreBase {
 mutable std::mutex storeMutex;
 std::string path, favoriteSleepImagePath, preferredSleepFolderPath;
 static const char* getFilePath(){return "/.crosspoint/state.json";}
 void toJson(JsonDocument& doc) const {doc["openEpubPath"]=path;}
 bool fromJson(JsonVariantConst doc) {path=doc["openEpubPath"] | "";return true;}
 bool saveSleepPreferencesLocked() const;
 bool saveSpecializedToFile() const;
 bool loadSpecializedFromFile();
 bool updateSleepFavoriteForPageOverlay(const std::string&);
 bool updateSleepFolderForPageOverlay(const std::string&);
 bool recoverSleepStateForPageOverlay();
};
''' + methods + r'''
int main() {
 constexpr const char* primary="/.crosspoint/state.json";
 constexpr const char* temp="/.crosspoint/state.json.tmp";
 constexpr const char* backup="/.crosspoint/state.json.bak";
 CrossPointState s;
 Storage.files[primary]=R"({"openEpubPath":"/old.epub"})";
 assert(s.loadSpecializedFromFile() && s.path=="/old.epub");
 s.path="/new.epub";
 Storage.files[temp]="partial";
 assert(!s.saveSpecializedToFile());
 assert(notice.pending()!=0);
 assert(Storage.files[primary]==R"({"openEpubPath":"/old.epub"})");
 Storage.files.erase(temp); Storage.files[backup]=Storage.files[primary]; Storage.files.erase(primary);
 notice.value=0;
 assert(s.loadSpecializedFromFile() && s.path=="/old.epub");
 assert(notice.pending()!=0);
 assert(!s.saveSpecializedToFile() && !Storage.files.count(primary));
 Storage.files[primary]="bad JSON";
 assert(s.loadSpecializedFromFile() && s.path=="/old.epub");
 Storage.files[primary]=R"({"openEpubPath":"/new.epub"})";
 assert(s.loadSpecializedFromFile() && s.path=="/new.epub");
 Storage.files.erase(backup); Storage.lookupFailure=true;
 assert(!s.saveSpecializedToFile());
 Storage.lookupFailure=false; assert(s.saveSpecializedToFile());
 assert(Storage.files[primary].find("/new.epub")!=std::string::npos);
 s.favoriteSleepImagePath="/old.bmp"; s.preferredSleepFolderPath="/old"; Storage.lookupFailure=true;
 assert(!s.updateSleepFavoriteForPageOverlay("/new.png") && s.favoriteSleepImagePath=="/old.bmp");
 assert(!s.updateSleepFolderForPageOverlay("/new") && s.preferredSleepFolderPath=="/old");
 Storage.lookupFailure=false;
 assert(s.updateSleepFavoriteForPageOverlay("/new.png") && s.favoriteSleepImagePath=="/new.png");
 assert(s.updateSleepFolderForPageOverlay("/new") && s.preferredSleepFolderPath=="/new");
 Storage.files.erase(primary); Storage.files["/.crosspoint/state.json.sleep-bak"]=R"({"openEpubPath":"/sleep.epub"})";
 assert(s.loadSpecializedFromFile() && s.path=="/sleep.epub");
 Storage.files[backup]=R"({"openEpubPath":"/move.epub"})";
 assert(!s.loadSpecializedFromFile());
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('State save fencing and backup fallback passed')
