"""Exercise production confirmation/notice setup and dismissal behavior."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/util/ConfirmationActivity.cpp').read_text()

def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

code = r'''
#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <vector>
struct Renderer{};struct Input{};
using GfxRenderer=Renderer;using MappedInputManager=Input;
struct ActivityResult {bool isCancelled=false;};
enum class StrId {STR_CANCEL,STR_CONFIRM};
struct {const char* get(StrId id){return id==StrId::STR_CANCEL?"Cancel":"Confirm";}} I18N;
struct Activity {
 Renderer& renderer;Input& mappedInput;int updates=0,finishes=0;ActivityResult result;
 Activity(const char*,Renderer& r,Input& i):renderer(r),mappedInput(i){}
 void onEnter(){}void requestUpdate(bool=false){++updates;}
 void finish(){++finishes;}void setResult(ActivityResult next){result=next;}
};
struct OptionPopup {
 std::string heading,body;std::vector<std::string> labels;std::function<void(int)> select;
 void show(const char* h,const char* b,const char** options,int count,int initial,std::function<void(int)> cb){
  assert(initial==0);heading=h;body=b;for(int n=0;n<count;++n)labels.emplace_back(options[n]);select=std::move(cb);
 }
 bool handleInput(Input&,std::function<void()>){return false;}bool isActive()const{return false;}
};
class ConfirmationActivity:public Activity {
 std::string heading,body,cancelLabel,confirmLabel;bool confirmOnly;
 public:
 OptionPopup confirmPopup;
 ConfirmationActivity(Renderer&,Input&,const std::string&,const std::string&,const std::string& ={},const std::string& ={},bool=false);
 void onEnter();void loop();
};
''' + '\n'.join(method(signature) for signature in (
    'ConfirmationActivity::ConfirmationActivity(', 'void ConfirmationActivity::onEnter()',
    'void ConfirmationActivity::loop()')) + r'''
int main(){
 Renderer renderer;Input input;
 ConfirmationActivity cancel(renderer,input,"Heading","Café body");cancel.onEnter();
 assert(cancel.confirmPopup.labels==std::vector<std::string>({"Cancel","Confirm"}));
 assert(cancel.confirmPopup.body=="Café body");cancel.confirmPopup.select(0);
 assert(cancel.result.isCancelled && cancel.finishes==1);
 ConfirmationActivity confirm(renderer,input,"Heading","Body","Back","Save");confirm.onEnter();
 assert(confirm.confirmPopup.labels==std::vector<std::string>({"Back","Save"}));confirm.confirmPopup.select(1);
 assert(!confirm.result.isCancelled && confirm.finishes==1);
 ConfirmationActivity notice(renderer,input,"Cannot move this filename","Turn off automatic moves","","OK",true);
 notice.onEnter();assert(notice.confirmPopup.labels==std::vector<std::string>({"OK"}));
 notice.confirmPopup.select(0);assert(!notice.result.isCancelled && notice.finishes==1);
 ConfirmationActivity back(renderer,input,"Heading","Body","","OK",true);back.onEnter();back.loop();
 assert(back.result.isCancelled && back.finishes==1);
}
'''
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', *shlex.split(os.environ.get('CXXFLAGS', '')),
                    str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
print('Confirmation preserves two-button choices; notices use one button and dismiss on Back')
