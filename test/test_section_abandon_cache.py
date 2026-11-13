"""Exercise Section's production error cleanup for speculative and foreground builds."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
def extract(signature):
    start=source.index(signature);opening=source.index('{',start);depth=0
    for index in range(opening,len(source)):
        depth+=(source[index]=='{')-(source[index]=='}')
        if not depth:return source[start:index+1]
    raise RuntimeError(signature)
method='\n'.join(extract(name) for name in ('void Section::abandonBuild()',
                                          'void Section::suspendBuild()', 'Section::~Section()'))
code = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
struct StorageStub {std::set<std::string> files;bool exists(const char* p){return files.contains(p);}bool remove(const char* p){return files.erase(p);}} Storage;
struct File {bool open=true;explicit operator bool()const{return open;}bool close(){open=false;return true;}};
#define LOG_INF(...) ((void)0)
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION=99;
struct Parser {static inline int aborted=0;void abortParse(){++aborted;}uint32_t parseBytesConsumed()const{return 123;}};
struct Css {static inline int cleared=0;void clear(){++cleared;}};
struct Build {std::unique_ptr<Parser> parser=std::make_unique<Parser>();std::unique_ptr<Css> cssParser=std::make_unique<Css>();bool reusedHtml=false;std::string tmpHtmlPath="/html.part";uint32_t totalBytes=456;};
struct Section {std::unique_ptr<Build> build_=std::make_unique<Build>();File file;std::string filePath="/chapter";
 bool buildComplete_=true,partial_=true;unsigned partialPageCount_=5,pageCount=5,builtPageCount_=3;
 uint32_t partialBytesConsumed_=80,partialTotalBytes_=400;bool buildPageWriteFailed_=false,commitOk=true;int commits=0;
 static inline unsigned publishedPages=0;
 std::string binTmpPath()const{return filePath+".part";}void abandonBuild();void suspendBuild();~Section();
 bool commitBuildFile(uint8_t version,uint32_t consumed,uint32_t total){
  ++commits;assert(version==SECTION_FILE_PARTIAL_VERSION && consumed==123 && total==456 && build_->parser);
  if(!commitOk)return false;
  publishedPages=builtPageCount_;file.close();Storage.files.erase(binTmpPath());return true;
 }
};
''' + method + r'''
int main(){
 for(bool partial:{false,true}){
  Storage.files={"/chapter","/chapter.part","/html.part"};Section section;section.partial_=partial;
  section.abandonBuild();assert(Storage.files.contains("/chapter"));
  assert(!Storage.files.contains("/chapter.part")&&!Storage.files.contains("/html.part"));
  assert(!section.build_&&!section.buildComplete_&&section.partial_==partial&&section.pageCount==(partial?5u:0u));
 }
 // The destructor used by prefetch suspension saves only fully built progress.
 for(bool partial:{false,true}){
  for(unsigned pages:{0u,3u,5u,8u}){
   for(bool commitOk:{false,true}){
    Storage.files={"/chapter","/chapter.part","/html.part"};Section::publishedPages=5;
    Section section;section.partial_=partial;section.builtPageCount_=pages;section.commitOk=commitOk;
    const bool worthKeeping=pages>0 && (!partial || pages>5);
    const int aborted=Parser::aborted,cleared=Css::cleared;
    section.suspendBuild();
    assert(section.commits==int(worthKeeping));
    const bool saved=worthKeeping&&commitOk;
    assert(Section::publishedPages==(saved?pages:5u));
    assert(section.pageCount==(saved?pages:(partial?5u:0u)));
    assert(section.partial_==(saved||partial));
    assert(section.partialBytesConsumed_==(saved?123u:80u));
    assert(section.partialTotalBytes_==(saved?456u:400u));
    assert(Parser::aborted==aborted+1 && Css::cleared==cleared+1);
    assert(!section.build_ && !section.file && !section.buildComplete_ && section.builtPageCount_==0);
    assert(Storage.files==std::set<std::string>{"/chapter"});
    section.suspendBuild();assert(Parser::aborted==aborted+1);
   }
  }
 }
 {Storage.files={"/chapter","/chapter.part","/html.part"};Section section;
  section.builtPageCount_=8;section.buildPageWriteFailed_=true;section.suspendBuild();
  assert(section.commits==0 && section.pageCount==5 && Storage.files==std::set<std::string>{"/chapter"});}
 {Storage.files={"/chapter","/chapter.part","/html.part"};Section section;
  section.build_->reusedHtml=true;section.suspendBuild();
  assert(Storage.files.contains("/html.part") && !section.build_);}
 {Storage.files={"/chapter","/chapter.part","/html.part"};Section::publishedPages=5;
  {Section section;section.builtPageCount_=8;}
  assert(Section::publishedPages==8 && Storage.files==std::set<std::string>{"/chapter"});}
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20',
                    *shlex.split(os.environ.get('CXXFLAGS','')),
                    str(path / 'test.cpp'),'-o',str(path / 'test')],check=True)
    subprocess.run([str(path / 'test')],check=True)
print('Section parser-error and destructor suspension checks passed')
