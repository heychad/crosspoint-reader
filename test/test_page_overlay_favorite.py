"""Exercise the image viewer's favorite save and in-memory failure rollback."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/util/BmpViewerActivity.cpp').read_text()
start = source.index('bool savePageOverlayFavorite(')
end = source.index('\n#endif', start)
method = source[source.index('bool pageOverlaySleepMode('):source.index('bool savePageOverlayFavorite(')] + source[start:end]
start = source.index('bool isPageOverlayFavorite(')
end = source.index('\nconst char* sleepCoverActionLabel', start)
method += '\n' + source[start:end]
program = r'''
#define CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY 1
#include <cassert>
#include <string>
#include <utility>
struct State {
 std::string favoriteSleepImagePath;
 bool ok=true;
 int saves=0;
 bool updateSleepFavoriteForPageOverlay(const std::string& path) { ++saves; if (ok) favoriteSleepImagePath=path; return ok; }
} APP_STATE;
struct CrossPointSettings {enum {PAGE_OVERLAY_SLEEP=14,COVER_OVERLAY_SLEEP=15};};
struct {int sleepScreen=14;} SETTINGS;
''' + method + r'''
int main() {
 APP_STATE.favoriteSleepImagePath="/sleep/old.bmp";
 APP_STATE.ok=false;
 assert(!savePageOverlayFavorite("/sleep/new.png"));
 assert(APP_STATE.favoriteSleepImagePath=="/sleep/old.bmp" && APP_STATE.saves==1);
 APP_STATE.ok=true;
 assert(savePageOverlayFavorite("/sleep/Café.png"));
 assert(APP_STATE.favoriteSleepImagePath=="/sleep/Café.png" && APP_STATE.saves==2);
 assert(isPageOverlayFavorite("/sleep/Café.png"));
 assert(!isPageOverlayFavorite("/sleep/other.png"));
 SETTINGS.sleepScreen=15;assert(isPageOverlayFavorite("/sleep/Café.png"));
 SETTINGS.sleepScreen=3; assert(!isPageOverlayFavorite("/sleep/Café.png"));
 assert(savePageOverlayFavorite(""));
 assert(APP_STATE.favoriteSleepImagePath.empty());
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Page overlay favorite save and failure rollback passed')
