"""Exercise Section's production error cleanup for speculative and foreground builds."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
start = source.index('void Section::abandonBuild()')
opening = source.index('{', start)
depth = 0
for index in range(opening, len(source)):
    depth += (source[index] == '{') - (source[index] == '}')
    if not depth:
        method = source[start:index + 1]
        break
else:
    raise RuntimeError('abandonBuild not found')
code = r'''
#include <cassert>
#include <memory>
#include <set>
#include <string>
struct StorageStub {std::set<std::string> files;bool exists(const char* p){return files.contains(p);}bool remove(const char* p){return files.erase(p);}} Storage;
struct File {bool open=true;explicit operator bool()const{return open;}bool close(){open=false;return true;}};
struct Parser {void abortParse(){}};struct Css {void clear(){}};
struct Build {std::unique_ptr<Parser> parser=std::make_unique<Parser>();std::unique_ptr<Css> cssParser=std::make_unique<Css>();bool reusedHtml=false;std::string tmpHtmlPath="/html.part";};
struct Section {std::unique_ptr<Build> build_=std::make_unique<Build>();File file;std::string filePath="/chapter";
 bool buildComplete_=true,partial_=true;unsigned partialPageCount_=5,pageCount=5,builtPageCount_=3;
 std::string binTmpPath()const{return filePath+".part";}void abandonBuild();};
''' + method + r'''
int main(){
 for(bool partial:{false,true}){
  Storage.files={"/chapter","/chapter.part","/html.part"};Section section;section.partial_=partial;
  section.abandonBuild();assert(Storage.files.contains("/chapter"));
  assert(!Storage.files.contains("/chapter.part")&&!Storage.files.contains("/html.part"));
  assert(!section.build_&&!section.buildComplete_&&section.partial_==partial&&section.pageCount==(partial?5u:0u));
 }
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(path / 'test.cpp'),'-o',str(path / 'test')],check=True)
    subprocess.run([str(path / 'test')],check=True)
print('Section parser-error cache preservation checks passed')
