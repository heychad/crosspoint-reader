"""Exercise the owned dictionary picker and reader preference handoff."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
picker = (root / "src/activities/reader/BookDictionarySelectActivity.cpp").read_text()
reader = (root / "src/activities/reader/EpubReaderActivity.cpp").read_text()
a = picker.index('void BookDictionarySelectActivity::onEnter()')
b = picker.index('void BookDictionarySelectActivity::buildScreen(', a)
c = reader.index('void EpubReaderActivity::openBookDictionary()')
d = reader.index('void EpubReaderActivity::openDictionaryWordSelect()', c)
code = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#define tr(x) #x
struct DictionaryEntry{std::string name,stem;};
std::vector<DictionaryEntry>available{{"English","en"},{"French","fr"},{std::string(32,'x'),"long"}};
namespace DictionaryRegistry{void discover(std::vector<DictionaryEntry>&out){out=available;}}
struct BookDictionaryResult{bool useGlobal=true;std::string name;};
struct ActivityResult{bool isCancelled=false;std::variant<std::monostate,BookDictionaryResult>data;};
namespace freeink{namespace ui{struct ListItem{const char*label=nullptr;int actionValue=-1;};}}
struct UiListActivity{void onEnter(){}};
struct BookDictionarySelectActivity:UiListActivity{
 bool useGlobal;std::string currentName;std::vector<DictionaryEntry>dictionaries;
 struct{int selected=-1;void reset(int i){selected=i;}}nav;
 bool finished=false;BookDictionaryResult result;
 BookDictionarySelectActivity(int&,int&,bool global,std::string name):useGlobal(global),currentName(name){}
 int listCount()const{return dictionaries.size()+2;}
 void onEnter();void activateIndex(int);static void provideRow(void*,uint16_t,freeink::ui::ListItem&);
 void setResult(BookDictionaryResult r){result=std::move(r);}void finish(){finished=true;}
};
struct ReaderPreferences{static constexpr unsigned DICTIONARY_BIT=1U<<16;};
struct BookReaderSettings{unsigned mask=0;};
struct{char dictionaryName[32]="English";unsigned mask=0;bool accepts=true,global=false;int changes=0;
 BookReaderSettings captureBookReaderSettings(){return{mask};}
 bool setBookDictionary(const char*name){++changes;global=!name;if(name)strcpy(dictionaryName,name);return accepts;}
}SETTINGS;
struct RenderLock{template<class T>explicit RenderLock(T&){} RenderLock()=default;};bool failAllocation=false;
template<class T,class...A>std::unique_ptr<T>makeUniqueNoThrow(A&&...args){if(failAllocation)return nullptr;return std::make_unique<T>(std::forward<A>(args)...);}
struct Flag{bool value=false;void store(bool b){value=b;}};
struct EpubReaderActivity{
 int renderer=0,mappedInput=0,saves=0,menus=0,updates=0,page=123;Flag pendingBookSettingsSaveError;
 std::unique_ptr<BookDictionarySelectActivity>child;std::function<void(const ActivityResult&)>callback;
 void openBookDictionary();void requestUpdate(){++updates;}void openReaderMenu(){++menus;}
 void saveBookReaderSettingsLocked(){++saves;}
 template<class F>void startActivityForResult(std::unique_ptr<BookDictionarySelectActivity>p,F f){child=std::move(p);callback=f;}
 void complete(ActivityResult result){auto f=callback;child.reset();f(result);}
};
''' + picker[a:b] + reader[c:d] + r'''
int main(){
 int renderer=0,input=0;BookDictionarySelectActivity p(renderer,input,false,"French");p.onEnter();
 assert(p.listCount()==4&&p.nav.selected==3);
 freeink::ui::ListItem row;p.provideRow(&p,2,row);assert(std::string(row.label)=="English");
 p.activateIndex(-1);p.activateIndex(4);assert(!p.finished);
 p.activateIndex(0);assert(p.result.useGlobal&&p.result.name.empty());
 p.finished=false;p.activateIndex(1);assert(p.finished&&!p.result.useGlobal&&p.result.name.empty());
 p.activateIndex(3);assert(!p.result.useGlobal&&p.result.name=="French");
 EpubReaderActivity r;r.openBookDictionary();assert(r.child&&r.child->useGlobal);r.complete({true,{}});assert(r.saves==0&&SETTINGS.changes==0);
 SETTINGS.mask=ReaderPreferences::DICTIONARY_BIT;r.openBookDictionary();assert(!r.child->useGlobal);
 r.complete({false,BookDictionaryResult{false,"French"}});assert(r.saves==1&&!SETTINGS.global&&std::string(SETTINGS.dictionaryName)=="French");
 r.openBookDictionary();r.complete({false,BookDictionaryResult{true,{}}});assert(r.saves==2&&SETTINGS.global);
 r.openBookDictionary();r.complete({false,BookDictionaryResult{false,{}}});assert(r.saves==3&&!SETTINGS.global&&SETTINGS.dictionaryName[0]=='\0');
 SETTINGS.accepts=false;r.openBookDictionary();r.complete({false,BookDictionaryResult{false,"French"}});assert(r.saves==3&&r.pendingBookSettingsSaveError.value);
 failAllocation=true;r.openBookDictionary();assert(r.updates==1&&r.page==123);
}
'''
with tempfile.TemporaryDirectory() as path:
    path = Path(path)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('Book dictionary picker, inheritance, None, and reader handoff passed')
