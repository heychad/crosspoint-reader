"""Exercise the production import dialogs without a reader or settings writes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/settings/SettingsActivity.cpp').read_text()


def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for pos in range(opening, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if depth == 0:
            return source[start:pos + 1]
    raise ValueError(signature)


methods = '\n'.join(extract(signature) for signature in (
    'bool SettingsActivity::handleCustomInput()',
    'void SettingsActivity::showCrossInkMessage(',
    'void SettingsActivity::openCrossInkControlsImport()',
    'void SettingsActivity::openCrossInkBarsImport()',
    'void SettingsActivity::openCrossInkLibraryImport()',
    'void SettingsActivity::openCrossInkPreferencesImport()',
    'void SettingsActivity::openCrossInkControlsRecovery()',
))
fixture = r'''
#include <cassert>
#include <functional>
#include <string>
#include <CrossInkImportTransaction.h>
#include <CrossInkImportGuard.h>
enum class StrId { STR_NONE_OPT, STR_RETRY, STR_OK_BUTTON, STR_SETTINGS_TITLE, STR_SETTINGS_RECOVERY_DETAILS,
  STR_IMPORT_CROSSINK_CONTROLS, STR_IMPORT_CROSSINK_PROMPT, STR_IMPORT_CROSSINK_CONFIRM, STR_CANCEL,
  STR_IMPORT_CROSSINK_DONE, STR_IMPORT_CROSSINK_UNCHANGED, STR_IMPORT_CROSSINK_MISSING,
  STR_IMPORT_CROSSINK_SOURCE_RECOVERY, STR_IMPORT_CROSSINK_INVALID, STR_IMPORT_CROSSINK_UNAVAILABLE,
  STR_IMPORT_CROSSINK_BACKUP_FAILED, STR_IMPORT_CROSSINK_REVIEW, STR_RECOVER_CROSSINK_IMPORT,
  STR_RECOVER_CROSSINK_PROMPT, STR_RECOVER_CROSSINK_CONFIRM, STR_RECOVER_CROSSINK_DONE,
  STR_RECOVER_CROSSINK_NOT_NEEDED, STR_RECOVER_CROSSINK_REVIEW, STR_RECOVER_CROSSINK_UNAVAILABLE,
  STR_IMPORT_CROSSINK_BARS, STR_IMPORT_CROSSINK_BARS_PROMPT, STR_IMPORT_CROSSINK_BARS_DONE,
  STR_IMPORT_CROSSINK_BARS_UNCHANGED, STR_IMPORT_CROSSINK_BARS_MISSING, STR_IMPORT_CROSSINK_BARS_UNSUPPORTED,
  STR_IMPORT_CROSSINK_LIBRARY, STR_IMPORT_CROSSINK_LIBRARY_PROMPT, STR_IMPORT_CROSSINK_LIBRARY_DONE,
  STR_IMPORT_CROSSINK_LIBRARY_UNCHANGED, STR_IMPORT_CROSSINK_PREFERENCES, STR_IMPORT_CROSSINK_PREFERENCES_PROMPT,
  STR_IMPORT_CROSSINK_PREFERENCES_DONE, STR_IMPORT_CROSSINK_PREFERENCES_UNCHANGED };
struct I18n { const char* get(StrId id) { static const auto values=[] { std::array<std::string,64> out{};
  for(size_t i=0;i<out.size();++i)out[i]=std::to_string(i);return out; }();
  return values[static_cast<int>(id)].c_str(); } } I18N;
#define tr(id) I18N.get(StrId::id)
struct Input {};
struct OptionPopup {
  bool active=false, inCallback=false; int selected=-1; std::string title, message; std::function<void(int)> callback;
  bool isActive() const{return active;}
  template<class F> bool handleInput(Input&,F){return false;}
  void showMessage(const char* heading, const char* text, const char* const*, int, int index, std::function<void(int)> fn){
    assert(!inCallback); active=true; selected=index; title=heading; message=text; callback=std::move(fn);
  }
  void choose(int index){ assert(active); active=false; inCallback=true; callback(index); inCallback=false; }
};
struct Notice { uint32_t value=0; uint32_t pending(){return value;} void acknowledge(uint32_t old){if(value==old)value=0;} } notice;
Notice& settingsSaveNotice(){return notice;}
struct CrossPointSettings {
  crossinkimport::ApplyResult result=crossinkimport::ApplyResult::Published;
  crossinkimport::RecoveryResult recovery=crossinkimport::RecoveryResult::Recovered;
  int imports=0, recoveries=0, saves=0; uint8_t uiTheme=0, nextTheme=0;
  crossinkimport::ApplyResult importCrossInkControls(char (&)[crossinkimport::SNAPSHOT_PATH_SIZE]){++imports;return result;}
  crossinkimport::ApplyResult importCrossInkBars(char (&)[crossinkimport::SNAPSHOT_PATH_SIZE]){++imports;return result;}
  crossinkimport::ApplyResult importCrossInkLibrary(char (&)[crossinkimport::SNAPSHOT_PATH_SIZE]){++imports;if(result==crossinkimport::ApplyResult::Published)uiTheme=nextTheme;return result;}
  crossinkimport::ApplyResult importCrossInkPreferences(char (&)[crossinkimport::SNAPSHOT_PATH_SIZE]){++imports;return result;}
  crossinkimport::RecoveryResult recoverUnchangedCrossInkImport(){++recoveries;return recovery;}
  bool saveToFile(){++saves;return false;}
} SETTINGS;
class SettingsActivity {
 public:
  OptionPopup optionPopup; Input mappedInput; StrId pendingCrossInkMessage=StrId::STR_NONE_OPT; StrId pendingCrossInkTitle=StrId::STR_IMPORT_CROSSINK_CONTROLS; int rebuilt=0, themeReloaded=0;
  void applyUiSettingChange(uint8_t CrossPointSettings::* field){assert(field==&CrossPointSettings::uiTheme);++themeReloaded;}
  void requestUpdate(){} void rebuildSettings(){++rebuilt;}
  bool handleCustomInput(); void showCrossInkMessage(StrId, StrId = StrId::STR_IMPORT_CROSSINK_CONTROLS); void openCrossInkControlsImport();
  void openCrossInkBarsImport();
  void openCrossInkLibraryImport();
  void openCrossInkPreferencesImport();
  void openCrossInkControlsRecovery();
};
''' + methods + r'''
int main(){
  SettingsActivity screen;
  screen.openCrossInkControlsImport();assert(screen.optionPopup.selected==1);
  screen.optionPopup.choose(1);assert(SETTINGS.imports==0);
  screen.openCrossInkControlsImport();screen.optionPopup.choose(0);
  assert(SETTINGS.imports==1 && screen.rebuilt==1 && screen.pendingCrossInkMessage==StrId::STR_IMPORT_CROSSINK_DONE);
  assert(screen.handleCustomInput() && screen.optionPopup.active);
  assert(screen.optionPopup.message==std::to_string(static_cast<int>(StrId::STR_IMPORT_CROSSINK_DONE)));
  screen.optionPopup.choose(0);
  SETTINGS.result=crossinkimport::ApplyResult::WriteUncertain;
  screen.openCrossInkControlsImport();screen.optionPopup.choose(0);
  assert(screen.pendingCrossInkMessage==StrId::STR_IMPORT_CROSSINK_REVIEW);
  assert(screen.handleCustomInput());
  assert(screen.optionPopup.title==std::to_string(static_cast<int>(StrId::STR_IMPORT_CROSSINK_CONTROLS)));
  screen.optionPopup.choose(0);
  notice.value=42;screen.openCrossInkControlsRecovery();assert(screen.optionPopup.selected==1);
  screen.optionPopup.choose(0);assert(SETTINGS.recoveries==1 && notice.value==42);
  assert(screen.pendingCrossInkMessage==StrId::STR_RECOVER_CROSSINK_DONE && SETTINGS.saves==0);
  assert(screen.handleCustomInput());
  assert(screen.optionPopup.title==std::to_string(static_cast<int>(StrId::STR_RECOVER_CROSSINK_IMPORT)));
  screen.optionPopup.choose(0);notice.value=0;
  SETTINGS.recovery=crossinkimport::RecoveryResult::NeedsReview;notice.value=43;
  screen.openCrossInkControlsRecovery();screen.optionPopup.choose(0);
  assert(notice.value==43 && screen.pendingCrossInkMessage==StrId::STR_RECOVER_CROSSINK_REVIEW);
  assert(screen.handleCustomInput());screen.optionPopup.choose(0);notice.value=0;
  SETTINGS.result=crossinkimport::ApplyResult::Unsupported;
  screen.openCrossInkBarsImport();assert(screen.optionPopup.selected==1);int oldImports=SETTINGS.imports;
  screen.optionPopup.choose(1);assert(SETTINGS.imports==oldImports);
  screen.openCrossInkBarsImport();screen.optionPopup.choose(0);
  assert(screen.pendingCrossInkMessage==StrId::STR_IMPORT_CROSSINK_BARS_UNSUPPORTED && screen.handleCustomInput());
  assert(screen.optionPopup.title==std::to_string(static_cast<int>(StrId::STR_IMPORT_CROSSINK_BARS)));
  screen.optionPopup.choose(0);
  SETTINGS.result=crossinkimport::ApplyResult::Published;
  screen.openCrossInkBarsImport();screen.optionPopup.choose(0);
  assert(screen.pendingCrossInkMessage==StrId::STR_IMPORT_CROSSINK_BARS_DONE && screen.handleCustomInput());
  screen.optionPopup.choose(0);
  SETTINGS.result=crossinkimport::ApplyResult::Published;
  screen.openCrossInkLibraryImport();assert(screen.optionPopup.selected==1);int beforeLibrary=SETTINGS.imports;
  screen.optionPopup.choose(1);assert(SETTINGS.imports==beforeLibrary);
  SETTINGS.nextTheme=4;screen.openCrossInkLibraryImport();screen.optionPopup.choose(0);
  assert(screen.themeReloaded==1 && SETTINGS.uiTheme==4);
  assert(screen.pendingCrossInkMessage==StrId::STR_IMPORT_CROSSINK_LIBRARY_DONE && screen.handleCustomInput());
  assert(screen.optionPopup.title==std::to_string(static_cast<int>(StrId::STR_IMPORT_CROSSINK_LIBRARY)));
  screen.optionPopup.choose(0);
  using R=crossinkimport::ApplyResult;
  for(const auto [result, expected] : {std::pair{R::NoChanges,StrId::STR_IMPORT_CROSSINK_LIBRARY_UNCHANGED},
      {R::SourceMissing,StrId::STR_IMPORT_CROSSINK_MISSING},
      {R::SourceRecovery,StrId::STR_IMPORT_CROSSINK_SOURCE_RECOVERY},
      {R::InvalidSource,StrId::STR_IMPORT_CROSSINK_INVALID},
      {R::Unavailable,StrId::STR_IMPORT_CROSSINK_UNAVAILABLE},
      {R::SnapshotFailed,StrId::STR_IMPORT_CROSSINK_BACKUP_FAILED},
      {R::RecoveryRequired,StrId::STR_IMPORT_CROSSINK_REVIEW},
      {R::WriteUncertain,StrId::STR_IMPORT_CROSSINK_REVIEW}}) {
    SETTINGS.result=result;screen.openCrossInkLibraryImport();screen.optionPopup.choose(0);
    assert(screen.pendingCrossInkMessage==expected && screen.handleCustomInput());screen.optionPopup.choose(0);
  }
  for(const auto [result, expected] : {std::pair{R::NoChanges,StrId::STR_IMPORT_CROSSINK_UNCHANGED},
      {R::SourceMissing,StrId::STR_IMPORT_CROSSINK_MISSING},
      {R::SourceRecovery,StrId::STR_IMPORT_CROSSINK_SOURCE_RECOVERY},
      {R::InvalidSource,StrId::STR_IMPORT_CROSSINK_INVALID},
      {R::Unavailable,StrId::STR_IMPORT_CROSSINK_UNAVAILABLE},
      {R::SnapshotFailed,StrId::STR_IMPORT_CROSSINK_BACKUP_FAILED},
      {R::RecoveryRequired,StrId::STR_IMPORT_CROSSINK_REVIEW}}) {
    SETTINGS.result=result;screen.openCrossInkControlsImport();screen.optionPopup.choose(0);
    assert(screen.pendingCrossInkMessage==expected && screen.handleCustomInput());screen.optionPopup.choose(0);
  }
  using Q=crossinkimport::RecoveryResult;
  for(const auto [result, expected] : {std::pair{Q::NotNeeded,StrId::STR_RECOVER_CROSSINK_NOT_NEEDED},
                                      {Q::Unavailable,StrId::STR_RECOVER_CROSSINK_UNAVAILABLE}}) {
    SETTINGS.recovery=result;screen.openCrossInkControlsRecovery();screen.optionPopup.choose(0);
    assert(screen.pendingCrossInkMessage==expected && screen.handleCustomInput());screen.optionPopup.choose(0);
  }
  SETTINGS.result=R::Published; const int before=SETTINGS.imports;
  screen.openCrossInkPreferencesImport(); assert(screen.optionPopup.selected==1);
  screen.optionPopup.choose(1); assert(SETTINGS.imports==before);
  screen.openCrossInkPreferencesImport(); screen.optionPopup.choose(0);
  assert(SETTINGS.imports==before+1 && screen.pendingCrossInkMessage==StrId::STR_IMPORT_CROSSINK_PREFERENCES_DONE);
  assert(screen.handleCustomInput()); screen.optionPopup.choose(0);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(fixture)
    subprocess.run(['c++', '-std=c++20', '-fsanitize=address', '-fno-omit-frame-pointer',
                    '-I', str(ROOT / 'src/personalization'), '-I', str(ROOT / '.pio/libdeps/x4pro/ArduinoJson/src'),
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('CrossInk import dialogs passed with AddressSanitizer')
