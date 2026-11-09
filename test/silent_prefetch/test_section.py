"""Exercise the production speculative section lifecycle against controllable storage."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
def method(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    end, depth = opening + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]
methods = '\n'.join(method(s) for s in [
    'uint32_t Section::onPageComplete(', 'bool Section::openSpeculativeOutput()',
    'void Section::discardOpenBuildOutput()', 'bool Section::closeSpeculativeOutput()',
    'bool Section::reopenSpeculativeOutput()', 'bool Section::buildSomeMore(',
    'bool Section::buildSomeMoreInternal(', 'bool Section::finalizeBuild()',
    'void Section::suspendBuild()', 'void Section::abandonBuild()'])
code = r'''
#include <cassert>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
constexpr int SECTION_FILE_VERSION=1,SECTION_FILE_PARTIAL_VERSION=2;
constexpr int O_RDWR=1,O_CREAT=2,O_EXCL=4;
struct File {bool open=true;operator bool()const{return open;}bool close(){open=false;return true;}
 bool sync(){return true;}uint32_t position(){return 5;}uint64_t fileSize64(){return 5;}
 bool seek64(uint64_t p){return p==5;}bool isDirectory(){return false;}};
struct HalStorage {
 enum class PathStatus {Missing,Present,Failed};
 struct MutationMarker{int epoch=0;};
 std::set<std::string> files;bool failLookup=false,locked=false;int removes=0,epoch=0;
 bool exists(const char* p){return files.count(p);}
 bool remove(const char* p){++removes;files.erase(p);return true;}
 bool rename(const char* a,const char* b){files.erase(a);files.insert(b);return true;}
 PathStatus lookupPath(const char* p){assert(locked);return failLookup?PathStatus::Failed:exists(p)?PathStatus::Present:PathStatus::Missing;}
 bool withStorageLock(bool(*f)(void*),void* ctx){bool old=locked;locked=true;bool r=f(ctx);locked=old;return r;}
 bool monitoredStorageHealthy(){return true;}bool captureMutationMarker(MutationMarker& m){m.epoch=epoch;return true;}
 bool mutationMarkerUnchanged(MutationMarker& m){return m.epoch==epoch;}
 File open(const char* p,int flags){assert(locked);++epoch;if(flags==(O_RDWR|O_CREAT|O_EXCL)){assert(!exists(p));files.insert(p);}
  else {assert(flags==O_RDWR && exists(p));}return {};}
} Storage;
struct Page {bool serialize(File&){return true;}};
struct Parser {
 enum class ParseStatus {Error,Done,More};
 ParseStatus status=ParseStatus::More;std::vector<int> anchors;size_t consumed=0;bool reject=false,stop=false;
 void requestStop(){stop=true;}void abortParse(){}void setRejectSpeculativeImages(){reject=true;}
 size_t parseBytesConsumed(){return consumed;}const auto& getAnchors()const{return anchors;}
 ParseStatus parseStep(){return stop?ParseStatus::Error:status;}bool finishParse(){return !stop;}
};
using ChapterHtmlSlimParser=Parser;
struct Css {void clear(){}};
struct Section {
 struct Context {std::unique_ptr<Parser> parser=std::make_unique<Parser>();Css* cssParser=nullptr;bool reusedHtml=true;
  std::string htmlPath="html",tmpHtmlPath="html.tmp";uint32_t bytesConsumed=0,totalBytes=0;};
 std::unique_ptr<Context> build_=std::make_unique<Context>();File file;std::string filePath="out",readFilePath_;
 uint16_t speculativeMaxPages_=0,speculativeMaxAnchors_=0,builtPageCount_=0,pageCount=0,partialPageCount_=0;
 HalStorage::MutationMarker speculativeMarker_;uint64_t speculativeSize_=5,speculativePosition_=5;bool preserveSpeculativeOutput_=false;
 uint32_t partialBytesConsumed_=0,partialTotalBytes_=0;bool partial_=false,buildComplete_=false;int commits=0;
 std::string speculativeFilePath()const{return filePath+".prefetch";}
 std::string binTmpPath()const{return speculativeMaxPages_ ? speculativeFilePath() : filePath+".part";}
 bool commitBuildFile(int version,uint32_t,uint32_t){if(speculativeMaxPages_)assert(Storage.locked);++commits;
  if(!speculativeMaxPages_)Storage.files.insert(filePath);return version==SECTION_FILE_VERSION || version==SECTION_FILE_PARTIAL_VERSION;}
 uint32_t onPageComplete(std::unique_ptr<Page>);
 bool openSpeculativeOutput();bool buildSomeMore(int,bool(*)());bool buildSomeMoreInternal(int,bool(*)());bool finalizeBuild();
 bool closeSpeculativeOutput();bool reopenSpeculativeOutput();void discardOpenBuildOutput();
 bool configureSpeculativePrefetch(uint16_t pages,uint16_t anchors){speculativeMaxPages_=pages;speculativeMaxAnchors_=anchors;
  build_->parser->reject=true;file.open=false;Storage.files.insert(speculativeFilePath());Storage.captureMutationMarker(speculativeMarker_);return true;}
 void suspendBuild();void abandonBuild();
};
''' + methods + r'''
void reset(){Storage.files={"old","out","out.part"};Storage.failLookup=false;Storage.locked=false;Storage.removes=0;Storage.epoch=0;}
int main(){
 reset();Section invalid;assert(invalid.configureSpeculativePrefetch(2,2));
 // Existing targets or temporary files are never opened or removed.
 assert(!invalid.openSpeculativeOutput());assert(Storage.exists("out") && Storage.exists("out.part"));
 Storage.files.erase("out");assert(!invalid.openSpeculativeOutput());assert(Storage.exists("out.part"));
 Storage.files.erase("out.part");Storage.failLookup=true;assert(!invalid.openSpeculativeOutput());
 Storage.failLookup=false;assert(!invalid.openSpeculativeOutput());Storage.files.erase("out.prefetch");
 assert(invalid.openSpeculativeOutput() && Storage.exists("out.prefetch"));
 assert(invalid.closeSpeculativeOutput());
 // An image or other parse failure must discard only speculative output.
 invalid.build_->parser->status=Parser::ParseStatus::Error;assert(!invalid.buildSomeMore(1,nullptr));
 assert(Storage.exists("old") && !Storage.exists("out.prefetch") && invalid.commits==0);
 // A page callback stops before serializing over the page budget.
 reset();Section cap;assert(cap.configureSpeculativePrefetch(1,1));cap.file.open=true;
 assert(cap.onPageComplete(std::make_unique<Page>())==5);
 assert(cap.onPageComplete(std::make_unique<Page>())==0 && cap.build_->parser->stop && cap.builtPageCount_==1);
 cap.file.open=false;
 assert(!cap.buildSomeMore(1,nullptr));assert(Storage.exists("old"));
 // Excess anchors and an explicit suspend cannot publish partial speculative pages.
 reset();Section anchors;assert(anchors.configureSpeculativePrefetch(2,1));anchors.build_->parser->anchors={1,2};
 assert(!anchors.buildSomeMore(1,nullptr));assert(Storage.exists("old") && anchors.commits==0);
 reset();Section suspended;assert(suspended.configureSpeculativePrefetch(2,2));suspended.builtPageCount_=1;
 suspended.suspendBuild();assert(Storage.exists("old") && suspended.commits==0 && !suspended.build_);
 // A failed lookup does not touch another book's cache.
 reset();Section fault;assert(fault.configureSpeculativePrefetch(2,2));Storage.failLookup=true;
 fault.build_->parser->status=Parser::ParseStatus::Done;assert(!fault.buildSomeMore(1,nullptr));assert(Storage.exists("old"));
 // The exclusively created destination publishes while holding the shared storage lock.
 reset();Section success;assert(success.configureSpeculativePrefetch(2,2));
 success.build_->parser->status=Parser::ParseStatus::Done;assert(success.buildSomeMore(1,nullptr));
 assert(success.commits==1 && success.buildComplete_ && Storage.exists("out.prefetch"));
 // Replacement after close must not be opened, written, or deleted.
 reset();Section replaced;assert(replaced.configureSpeculativePrefetch(2,2));++Storage.epoch;
 assert(!replaced.buildSomeMore(1,nullptr));assert(Storage.exists("out.prefetch") && Storage.exists("old"));
 // An unfinished parse closes its output before yielding; a later mutation invalidates it.
 reset();Section yielded;assert(yielded.configureSpeculativePrefetch(2,2));
 assert(yielded.buildSomeMore(1,[](){return true;}));assert(!yielded.file && yielded.build_);
 ++Storage.epoch;assert(!yielded.buildSomeMore(1,nullptr));assert(Storage.exists("out.prefetch") && Storage.exists("old"));
 // Normal parse failure retains its existing cleanup behavior.
 reset();Section ordinary;ordinary.build_->parser->status=Parser::ParseStatus::Error;
 assert(!ordinary.buildSomeMore(1,nullptr));assert(!Storage.exists("out") && Storage.exists("old"));
}
'''
with tempfile.TemporaryDirectory() as tmp:
    cpp, executable = Path(tmp) / 'section.cpp', Path(tmp) / 'section-test'
    cpp.write_text(code)
    subprocess.run(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print('Speculative section lifecycle checks passed')
