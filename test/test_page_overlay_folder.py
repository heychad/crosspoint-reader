"""Exercise sleep-folder selection and in-memory rollback on save failure."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/home/FileBrowserActivity.cpp').read_text()
start = source.index('bool savePreferredSleepFolder(')
end = source.index('\n#endif', start)
method = source[start:end]
program = r'''
#include <cassert>
#include <string>
struct State {
 std::string preferredSleepFolderPath;
 bool ok=true;
 int saves=0;
 bool updateSleepFolderForPageOverlay(const std::string& path) { ++saves; if (ok) preferredSleepFolderPath=path; return ok; }
} APP_STATE;
''' + method + r'''
int main() {
 APP_STATE.preferredSleepFolderPath="/old";
 APP_STATE.ok=false;
 assert(!savePreferredSleepFolder("/Café", false));
 assert(APP_STATE.preferredSleepFolderPath=="/old");
 assert(!savePreferredSleepFolder("/old", true));
 assert(APP_STATE.preferredSleepFolderPath=="/old");
 APP_STATE.ok=true;
 assert(savePreferredSleepFolder("/Café", false));
 assert(APP_STATE.preferredSleepFolderPath=="/Café");
 assert(savePreferredSleepFolder("/Café", true));
 assert(APP_STATE.preferredSleepFolderPath.empty() && APP_STATE.saves==4);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Page overlay folder save and failure rollback passed')
