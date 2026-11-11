"""Exercise reader state warnings and ordering with the production methods."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/reader/ReaderActivity.cpp').read_text()
def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for pos in range(opening, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if not depth:
            return source[start:pos + 1]
    raise ValueError(signature)

program = r'''
#define CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY 1
#include <atomic>
#include <cassert>
#include <cstdint>
struct Notice {uint32_t value=0;uint32_t pending(){return value;}} state,settings;
Notice& stateSaveNotice(){return state;} Notice& settingsSaveNotice(){return settings;}
enum {STR_STATE_SAVE_RECOVERY=1,STR_SETTINGS_SAVE_RECOVERY=2};
#define tr(x) (x)
struct UI {int shown=0;void drawPopup(int,int msg){shown=msg;}} GUI;
struct ReaderActivity {
 std::atomic<bool> settingsNoticeRenderReady{false},homeButtonFeedbackVisible{false};
 std::atomic<uint32_t> shownStateNotice{0},shownSettingsNotice{0};
 int renderer=0,updates=0;
 void requestUpdate(){++updates;}
 void pollSettingsSaveWarning();
 bool showPendingSettingsWarning();
};
''' + extract('void ReaderActivity::pollSettingsSaveWarning()') + '\n' + \
    extract('bool ReaderActivity::showPendingSettingsWarning()') + r'''
int main() {
 ReaderActivity r; state.value=1; r.pollSettingsSaveWarning(); assert(r.updates==0);
 r.settingsNoticeRenderReady=true; r.pollSettingsSaveWarning(); assert(r.updates==1);
 settings.value=2; assert(r.showPendingSettingsWarning() && GUI.shown==STR_STATE_SAVE_RECOVERY);
 assert(r.showPendingSettingsWarning() && GUI.shown==STR_SETTINGS_SAVE_RECOVERY);
 assert(!r.showPendingSettingsWarning());
 r.homeButtonFeedbackVisible=true; state.value=3; r.pollSettingsSaveWarning(); assert(r.updates==1);
 r.homeButtonFeedbackVisible=false; r.pollSettingsSaveWarning(); assert(r.updates==2);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Reader state warning gating and order passed')
