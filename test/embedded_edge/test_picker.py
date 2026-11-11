"""Exercise production edge metadata, picker callbacks, and stored-value validation."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
settings = (root / "src/SettingsList.h").read_text()
start = settings.index("inline void configureReaderEdgePicker(")
configure = settings[start:settings.index("// Build the font family", start)]
base = (root / "src/SettingsList.cpp").read_text()
start = base.index("    const auto edgeActions =")
base = base[start:base.index("\n  }", start)]
source = (root / "src/CrossPointSettings.cpp").read_text()
start = source.index("      const uint8_t fieldDefault =")
validation = source[start:source.index("\n    }\n  };", start)]
source = (root / "src/activities/settings/SettingsActivity.cpp").read_text()
start = source.index("    const uint8_t totalValues =", source.index("setting.valueGetter && setting.valueSetter"))
picker = source[start:source.index("\n  } else if (setting.type == SettingType::VALUE", start)]

code = r'''
#include <ArduinoJson.h>
#include <cassert>
#include <functional>
#include <memory>
#include <span>
#include <variant>
#include <vector>
#include "personalization/ReaderQuickBindings.h"
#include "personalization/ReaderShortcutLabels.h"
CrossPointSettings SETTINGS;
enum class SettingType {ENUM,TOGGLE,VALUE};
struct SettingInfo {
 StrId nameId; SettingType type=SettingType::ENUM;
 uint8_t CrossPointSettings::*valuePtr=nullptr;
 std::span<const StrId> staticEnumValues;
 std::vector<std::string> enumStringValues;
 const char*key=nullptr;
 struct {uint8_t min=0,max=255;} valueRange;
 std::function<uint8_t()>valueGetter;std::function<void(uint8_t)>valueSetter;
 auto enumLabels()const{return staticEnumValues;}
 static SettingInfo StaticEnum(StrId id,uint8_t CrossPointSettings::*ptr,std::span<const StrId> labels,const char*key,StrId){
  SettingInfo s;s.nameId=id;s.valuePtr=ptr;s.staticEnumValues=labels;s.key=key;return s;
 }
};
''' + configure + r'''
std::vector<SettingInfo> baseSettings(){
 std::vector<SettingInfo> result;
 const auto&readerActions=readershortcuts::ACTION_LABELS;
 auto emit=[&](SettingInfo info){result.push_back(info);};
''' + base + r'''
 return result;
}
void load(const JsonDocument&doc){
 auto&s=SETTINGS;
 auto clamp=[](uint8_t value,uint8_t count,uint8_t fallback){return value<count?value:fallback;};
 for(const auto&info:baseSettings()){
''' + validation + r'''
  readershortcuts::normalizeEdgeAction(s.*info.valuePtr);
 }
}
struct MenuResult {int action;};
struct ActivityResult {bool isCancelled=false;std::variant<std::monostate,MenuResult>data;};
struct EnumSelectionActivity {template<class...T>EnumSelectionActivity(T&&...){}};
bool failAllocation=false;
template<class T,class...Args>std::unique_ptr<T>makeUniqueNoThrow(Args&&...args){
 return failAllocation?nullptr:std::make_unique<T>(std::forward<Args>(args)...);
}
#define LOG_ERR(...) ((void)0)
struct OptionPopup {
 int shown=0;static constexpr int maxOptions(){return 16;}
 template<class...Args>void show(Args&&...){++shown;}
};
struct Screen {
 int renderer=0,mappedInput=0,rebuilds=0;
 SettingInfo setting;OptionPopup optionPopup;std::function<void(const ActivityResult&)>callback;
 void syncQuickResumeTimeoutForSleepScreen(bool,bool){}void rebuildSettings(){++rebuilds;}void requestUpdate(){}
 template<class F>void startActivityForResult(std::unique_ptr<EnumSelectionActivity>,F fn){callback=std::move(fn);}
 void choose(){
 const bool sleepScreenChanged=false,quickResumeTimeoutChanged=false;
''' + picker + r'''
 }
};
int main(){
 using A=readershortcuts::Action;
 for(auto raw: {42,43,44}){
  SETTINGS={};
  for(auto base:baseSettings()){
   assert(base.enumLabels().size()>static_cast<size_t>(raw));
   const auto field=base.valuePtr;
   SETTINGS.*field=raw;
   Screen screen;screen.setting=base;configureReaderEdgePicker(screen.setting);
   assert(screen.setting.valuePtr==nullptr&&screen.setting.enumLabels().size()==37);
   const auto index=screen.setting.valueGetter();
   assert(screen.setting.enumLabels()[index]==readershortcuts::ACTION_LABELS[raw]);
   screen.setting.valueSetter(0);assert(SETTINGS.*field==0);
   screen.choose();assert(screen.callback&&screen.optionPopup.shown==0);
   screen.callback({false,MenuResult{index}});assert(SETTINGS.*field==raw);
   const auto saves=SETTINGS.saves;
   screen.callback({true,MenuResult{0}});screen.callback({false,MenuResult{-1}});
   screen.callback({false,MenuResult{37}});assert(SETTINGS.*field==raw&&SETTINGS.saves==saves);
   JsonDocument saved;saved[base.key]=SETTINGS.*field;
   SETTINGS.*field=0;load(saved);assert(SETTINGS.*field==raw);
   failAllocation=true;Screen failed;failed.setting=screen.setting;failed.choose();
   assert(!failed.callback&&failed.optionPopup.shown==0&&SETTINGS.saves==saves);failAllocation=false;
  }
 }
 SETTINGS={};SETTINGS.readerUpTap=readerquickactions::QUICK;
 Screen screen;screen.setting=baseSettings()[0];configureReaderEdgePicker(screen.setting);screen.choose();
 screen.callback({false,MenuResult{21}});
 assert(SETTINGS.readerLeftEdgeUp==readerquickactions::QUICK&&SETTINGS.readerUpTap==0);
 for(unsigned raw=0;raw<=255;++raw){
  SETTINGS={};JsonDocument saved;saved["readerLeftEdgeUp"]=raw;load(saved);
  assert(SETTINGS.readerLeftEdgeUp==(readershortcuts::isEdgeAction(raw)?raw:0));
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    directory = Path(directory)
    (directory / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", "-I", str(root / "test/quick_actions/stubs"),
                    "-I", str(root / "src"), "-I", str(root / "lib/I18n"),
                    "-I", sys.argv[1], str(directory / "test.cpp"), "-o", str(directory / "test")], check=True)
    subprocess.run([str(directory / "test")], check=True)
print("Edge picker, cancellation, allocation failure, Quick Actions ownership, and reload passed")
