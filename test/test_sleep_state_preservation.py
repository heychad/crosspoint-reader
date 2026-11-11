"""Exercise state serialization with CrossInk's sleep-image preferences."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/CrossPointState.cpp').read_text()
methods = source[source.index('void CrossPointState::toJson('):]
program = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
#include <string>
class CrossPointState {
public:
 std::string openEpubPath, favoriteSleepImagePath, preferredSleepFolderPath;
 uint32_t sleepLcgCount=0, sleepLcgState=0, sleepOverlayLcgCount=0, sleepOverlayLcgState=0;
 uint8_t readerActivityLoadCount=0;
 bool lastSleepFromReader=false, showBootScreen=true;
 void toJson(JsonDocument&) const;
 bool fromJson(JsonVariantConst);
};
''' + methods + r'''
int main() {
 JsonDocument legacy;
 assert(!deserializeJson(legacy, R"({"openEpubPath":"/books/Book.epub","favoriteSleepImagePath":"/sleep/Café.png","preferredSleepFolderPath":"/sleep/Art","lastSleepFromReader":true})"));
 CrossPointState state;
 assert(state.fromJson(legacy.as<JsonVariantConst>()));
 JsonDocument saved;
 state.toJson(saved);
 assert(saved["openEpubPath"] == "/books/Book.epub");
 assert(saved["favoriteSleepImagePath"] == "/sleep/Café.png");
 assert(saved["preferredSleepFolderPath"] == "/sleep/Art");
 assert(saved["lastSleepFromReader"] == true);
 JsonDocument empty;
 empty.to<JsonObject>();
 assert(state.fromJson(empty.as<JsonVariantConst>()));
 assert(state.favoriteSleepImagePath.empty() && state.preferredSleepFolderPath.empty());
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('CrossInk sleep-image state round trip passed')
