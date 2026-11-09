"""Execute the production web settings mutation loop against real JSON and binding policy."""
from pathlib import Path
import subprocess,sys,tempfile
root=Path(__file__).resolve().parents[2]
source=(root/'src/network/CrossPointWebServer.cpp').read_text()
start=source.index('  const auto quickBindingsBefore = readerquickactions::bindingValues(SETTINGS);')
end=source.index('  LOG_DBG("WEB", "Applied',start)
body=source[start:end]
code=r'''
#include <ArduinoJson.h>
#include <cassert>
#include <functional>
#include <span>
#include <string>
#include <vector>
#include <cstring>
#include "personalization/ReaderQuickBindings.h"
CrossPointSettings SETTINGS;
enum {STR_WEB_SETTINGS_SAVE_UNCERTAIN};
const char* tr(int){return "settings failed";}
struct Server {int status=0;void send(int code,const char*,const char*){status=code;}} web;
Server* server=&web;
enum class SettingType{TOGGLE,ENUM,VALUE,STRING,ACTION};
struct Info{
 const char*key;SettingType type=SettingType::ENUM;uint8_t CrossPointSettings::*valuePtr=nullptr;
 bool webHidden=false;std::function<void(uint8_t)>valueSetter;std::function<void(const std::string&)>stringSetter;
 std::vector<int>values;std::vector<std::string>enumStringValues;size_t stringMaxLen=0,stringOffset=0;
 struct{int min=0,max=255;}valueRange;
 std::span<const int>enumLabels()const{return values;}
};
std::vector<Info> makeSettings(){
 std::vector<Info> result;
 for(auto pair:{std::pair{"readerUpTap",&CrossPointSettings::readerUpTap},{"readerDownTap",&CrossPointSettings::readerDownTap},{"readerDownHold",&CrossPointSettings::readerDownHold},{"readerLeftEdgeUp",&CrossPointSettings::readerLeftEdgeUp},{"homeButtonTapAction",&CrossPointSettings::homeButtonTapAction}}){
   Info info;info.key=pair.first;info.valuePtr=pair.second;info.values.resize(40);result.push_back(info);
 }
 Info power;power.key="shortPwrBtn";power.values.resize(7);power.valueSetter=[](uint8_t display){SETTINGS.shortPwrBtn=display==6?CrossPointSettings::PWR_QUICK_ACTIONS:CrossPointSettings::IGNORE;};result.push_back(power);
 return result;
}
void apply(const char*json){JsonDocument doc;assert(!deserializeJson(doc,json));const auto settings=makeSettings();
''' +body+r'''
}
int main(){
 using namespace readerquickactions;
 const auto home=static_cast<uint8_t>(HomeButtonAction::QuickActions);
 apply(R"({"readerUpTap":21})");assert(SETTINGS.readerUpTap==QUICK);
 apply(R"({"readerDownTap":21})");assert(SETTINGS.readerUpTap==0 && SETTINGS.readerDownTap==QUICK);
 apply(R"({"readerLeftEdgeUp":21})");assert(SETTINGS.readerDownTap==0 && SETTINGS.readerLeftEdgeUp==QUICK);
 apply(R"({"homeButtonTapAction":12})");assert(SETTINGS.readerLeftEdgeUp==0 && SETTINGS.homeButtonTapAction==home);
 apply(R"({"shortPwrBtn":6})");assert(SETTINGS.homeButtonTapAction==static_cast<uint8_t>(HomeButtonAction::Home) && SETTINGS.shortPwrBtn==CrossPointSettings::PWR_QUICK_ACTIONS);
 // A full client snapshot includes the unchanged old owner; the newly assigned side must still win.
 apply(R"({"readerUpTap":21,"shortPwrBtn":6,"homeButtonTapAction":0,"readerDownHold":12})");
 assert(SETTINGS.readerUpTap==QUICK && SETTINGS.shortPwrBtn==CrossPointSettings::IGNORE && SETTINGS.readerDownHold==12);
 apply(R"({"readerUpTap":21,"homeButtonTapAction":12})");assert(SETTINGS.readerUpTap==0 && SETTINGS.homeButtonTapAction==home);
 assert(SETTINGS.saves==7);
 SETTINGS.failSave=true;apply(R"({"readerDownHold":11})");assert(web.status==500&&SETTINGS.saves==8);
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);(p/'test.cpp').write_text(code)
 subprocess.run(['c++','-std=c++20','-I',str(root/'test/quick_actions/stubs'),'-I',str(root/'src'),'-I',sys.argv[1],str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('Web Quick Actions ownership and full-snapshot checks passed')
