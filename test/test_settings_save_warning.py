"""Exercise the settings-screen warning routing with the production handler."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()
start = text.index('bool SettingsActivity::handleCustomInput()')
end = text.index('\n}\n', start) + 2
method = text[start:end]
fixture = r'''
#include <StatsSaveNotice.h>
#include <cassert>
#include <functional>
#include <string>
uint32_t retained=0;
StatsSaveNotice& settingsSaveNotice(){static StatsSaveNotice notice(retained);return notice;}
enum { STR_RETRY, STR_OK_BUTTON, STR_SETTINGS_TITLE, STR_SETTINGS_RECOVERY_DETAILS };
const char* tr(int) { return "text"; }
struct Settings {bool succeeds=true;bool saveToFile(){if(!succeeds)settingsSaveNotice().recordFailure();return succeeds;}} SETTINGS;
struct Input { bool confirm=false; };
struct Popup {
 bool active=false; std::function<void(int)> callback;
 bool isActive() const {return active;}
 bool handleInput(Input& input,const std::function<void()>& update) {
  if(!active)return false;
  if(input.confirm){active=false;input.confirm=false;if(callback)callback(0);update();}
  return true;
 }
 void showMessage(const char*,const char*,const char**,int,int,std::function<void(int)> cb){active=true;callback=cb;}
};
struct SettingsActivity {Popup optionPopup;Input mappedInput;int updates=0;void requestUpdate(){++updates;}bool handleCustomInput();};
'''
body = r'''
int main(){SettingsActivity a;assert(!a.handleCustomInput());
 settingsSaveNotice().recordFailure();assert(a.handleCustomInput()&&a.optionPopup.active&&a.updates==1);
 auto first=settingsSaveNotice().pending();settingsSaveNotice().recordFailure();a.mappedInput.confirm=true;assert(a.handleCustomInput());assert(settingsSaveNotice().pending()!=0&&settingsSaveNotice().pending()!=first);
 assert(a.handleCustomInput()&&a.optionPopup.active);a.mappedInput.confirm=true;assert(a.handleCustomInput());assert(settingsSaveNotice().pending()==0);assert(!a.handleCustomInput());
 settingsSaveNotice().recordFailure();assert(a.handleCustomInput()&&a.optionPopup.active);SETTINGS.succeeds=false;a.mappedInput.confirm=true;assert(a.handleCustomInput());assert(settingsSaveNotice().pending()!=0);assert(a.handleCustomInput()&&a.optionPopup.active);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'fixture.cpp').write_text(fixture + method + body)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'src/activities/reader'), str(path / 'fixture.cpp'),
                    '-o', str(path / 'fixture')], check=True)
    subprocess.run([str(path / 'fixture')], check=True)
print('Settings warning routing passed under AddressSanitizer')
