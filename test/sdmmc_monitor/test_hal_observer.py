"""Exercise the actual HAL monitor handoff without touching the card."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/hal/HalStorage.cpp').read_text()

def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for end in range(opening, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise ValueError(signature)

methods = '\n'.join(extract(signature) for signature in (
    'bool HalStorage::begin()', 'bool HalStorage::ready() const', 'bool HalStorage::observedSdmmcHealthy() const'))
code = r'''
#include <cassert>
bool armed=false, healthy=true;
extern "C" void crosspoint_sdmmc_monitor_arm(){armed=true;}
extern "C" bool crosspoint_sdmmc_monitor_healthy(){return healthy;}
struct Card {bool mounted=false;bool begin(){mounted=true;return true;}bool ready(){return mounted;}} SDCard;
struct HalStorage {struct StorageLock{};bool begin();bool ready()const;bool observedSdmmcHealthy()const;};
''' + methods + r'''
int main(){
 HalStorage storage;assert(!storage.observedSdmmcHealthy());assert(storage.begin());
#if FREEINK_DEVICE_X4PRO
 assert(armed && storage.observedSdmmcHealthy());healthy=false;assert(!storage.observedSdmmcHealthy());
#else
 assert(!armed && !storage.observedSdmmcHealthy());
#endif
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    for device in (0, 1):
        subprocess.run(['c++', '-std=c++20', '-DFREEINK_DEVICE_X4PRO=' + str(device), str(path / 'test.cpp'),
                        '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)
print('HAL monitor arming and X4 Pro-only observation passed')
