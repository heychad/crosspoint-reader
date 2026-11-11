"""Check persisted power-action values across upstream integration."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
header = (root / 'src/CrossPointSettings.h').read_text()
enum = 'enum SHORT_PWRBTN {' + header.split('enum SHORT_PWRBTN {', 1)[1].split('};', 1)[0] + '};'
source = (root / 'src/CrossPointSettings.cpp').read_text()
start = source.index('  doublePwrBtn =\n')
end = source.index('\n', source.index('doublePwrBtn = PWR_FRONTLIGHT;', start))
code = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <cstdint>
''' + enum + r'''
uint8_t clamp(uint8_t n, uint8_t count, uint8_t fallback){return n<count?n:fallback;}
uint8_t load(JsonVariantConst doc){uint8_t doublePwrBtn;
''' + source[start:end] + r'''
return doublePwrBtn;}
int main(){
static_assert(PWR_QUICK_ACTIONS==10 && PWR_FRONTLIGHT==11);
JsonDocument doc;doc.to<JsonObject>();assert(load(doc)==PWR_FRONTLIGHT);
doc["doublePwrBtn"]=10;assert(load(doc)==PWR_FRONTLIGHT);
doc["powerActionSchema"]=1;assert(load(doc)==PWR_QUICK_ACTIONS);
doc["doublePwrBtn"]=11;assert(load(doc)==PWR_FRONTLIGHT);
doc["doublePwrBtn"]=255;assert(load(doc)==PWR_FRONTLIGHT);
doc.clear();doc["doubleClickPwrLight"]=0;assert(load(doc)==IGNORE);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Power action migration preserves local Quick Actions and upstream frontlight')
