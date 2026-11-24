"""Fault-test the production section-cache commit and publication logic."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
def extract(signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if not depth:
            return source[start:index + 1]
    raise RuntimeError(signature)
code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION=99;
constexpr size_t HEADER_SIZE=46;
struct ReadFault {
 int seekAt=-1, readAt=-1, seeks=0, reads=0;
} fault;
struct HalStorage {
 enum class PathStatus{Missing,Present,Failed};std::map<std::string,std::string> files;
 PathStatus lookupPath(const char* p){return files.contains(p)?PathStatus::Present:PathStatus::Missing;}
 bool rename(const char* a,const char* b){if(!files.contains(a)||files.contains(b))return false;files[b]=files[a];files.erase(a);return true;}
 bool remove(const char* p){return files.erase(p)>0;}
 bool openFileForRead(const char*,const std::string&,struct HalFile&);
} Storage;
struct HalFile {
 std::string path; size_t pos=64;int calls=0, failAt=-1, seeks=0, failSeek=-1;bool syncOk=true,closeOk=true;
 explicit operator bool()const{return !path.empty();}
 size_t position()const{return pos;}
 uint64_t fileSize64() const {return fileSize();}
 size_t read(void* buffer,size_t count) {
   const auto& data=Storage.files.at(path);
   if (pos>data.size()) return 0;
   const size_t available=std::min(count,data.size()-pos);
   const size_t copied=(++fault.reads==fault.readAt && available) ? available-1 : available;
   std::memcpy(buffer,data.data()+pos,copied);
   pos+=copied;
   return copied;
 }
 bool seek(size_t n){if(seeks++==failSeek || fault.seeks++==fault.seekAt)return false;pos=n;return true;}
 size_t write(const uint8_t* data,size_t n){if(calls++==failAt)return 0;auto& s=Storage.files[path];if(s.size()<pos+n)s.resize(pos+n);s.replace(pos,n,reinterpret_cast<const char*>(data),n);pos+=n;return n;}
 bool sync(){return syncOk;}bool close(){return closeOk;}
 size_t fileSize()const{return Storage.files[path].size();}
};
bool HalStorage::openFileForRead(const char*,const std::string& p,HalFile& f){if(!files.contains(p))return false;f.path=p;return true;}
namespace serialization {
 template<class T>bool tryReadPod(HalFile& f,T& value){return f.read(&value,sizeof(value))==sizeof(value);}
 template<class T>bool tryWritePod(HalFile& f,const T& t){return f.write(reinterpret_cast<const uint8_t*>(&t),sizeof(T))==sizeof(T);}
 bool tryWriteString(HalFile& f,const std::string& s){uint32_t n=s.size();return tryWritePod(f,n) && (!n || f.write(reinterpret_cast<const uint8_t*>(s.data()),n)==n);}
}
struct Parser {std::vector<std::pair<std::string,uint16_t>> anchors{{"part",0}};const auto& getAnchors(){return anchors;}};
struct Entry{uint32_t fileOffset;uint16_t paragraphIndex,listItemIndex;uint32_t visibleTextOffset;};
struct Page {
 uint32_t visibleTextOffset=0;
 uint8_t marker=0;
 bool serialize(HalFile& f){return f.write(&marker,sizeof(marker))==sizeof(marker);}
 static std::unique_ptr<Page> deserialize(HalFile& f) {
   auto page=std::make_unique<Page>();
   return serialization::tryReadPod(f,page->marker) ? std::move(page) : nullptr;
 }
};
struct Build{std::vector<Entry> lut{{48,1,2,3},{58,4,5,6}};std::unique_ptr<Parser> parser=std::make_unique<Parser>();};
enum class EpubBuildFailure {None, InputOutput};
struct Section {
 bool buildPageWriteFailed_=false;
 EpubBuildFailure buildFailure_=EpubBuildFailure::None;
 std::unique_ptr<Page> loadPageDuringBuild(int);
 uint32_t onPageComplete(std::unique_ptr<Page>);
 bool partial_=false;
 uint16_t partialPageCount_=0,pageCount=2;
 std::unique_ptr<Page> loadPageAt(int page) const;
 HalFile file;std::string filePath="/chapter";std::unique_ptr<Build> build_=std::make_unique<Build>();uint16_t builtPageCount_=2;
 std::string binTmpPath()const{return filePath+".part";}bool commitBuildFile(uint8_t,uint32_t,uint32_t);};
''' + extract('struct SectionPageTables') + ';\n' + extract('uint32_t Section::onPageComplete(') + '\n' + extract('std::unique_ptr<Page> Section::loadPageDuringBuild(') + '\n' + extract('std::unique_ptr<Page> Section::loadPageAt(') + '\n' + extract('bool recoverSectionCache(') + '\n' + extract('bool promoteSectionCache(') + '\n' + extract('bool Section::commitBuildFile(') + r'''
void checkActivePageRead() {
 for(int failedSeek : {-1,0,1}) {
   Section s; Storage={}; fault={};
   auto& bytes=Storage.files[s.binTmpPath()]; bytes=std::string(64,0);bytes[58]='B';
   const auto original=bytes;
   s.file.path=s.binTmpPath();s.file.failSeek=failedSeek;
   auto page=s.loadPageDuringBuild(1);
   assert(fault.reads==(failedSeek==0 ? 0 : 1));
   assert(bool(page)==(failedSeek<0));
   if(page) assert(page->marker=='B' && page->visibleTextOffset==6);
   assert(s.buildPageWriteFailed_==(failedSeek==1));
   if(failedSeek==1) {
     assert(s.buildFailure_==EpubBuildFailure::InputOutput && !s.loadPageDuringBuild(0));
     assert(!s.onPageComplete(std::make_unique<Page>()) && !s.file.calls);
   } else {
     assert(s.file.position()==64);
     fault={};fault.readAt=1;
     assert(!s.loadPageDuringBuild(1) && !s.buildPageWriteFailed_ && s.file.position()==64);
   }
   assert(bytes==original);
   if(failedSeek!=1) {
     auto next=std::make_unique<Page>();next->marker='C';
     assert(s.onPageComplete(std::move(next))==64 && bytes==original+"C");
   }
 }
 fault={};
}
void checkCachedPages(bool partial) {
 Section s;
 Storage={};
 auto& staged=Storage.files[s.binTmpPath()];
 staged=std::string(64,0);
 staged[48]='A'; staged[58]='B';
 s.file.path=s.binTmpPath();
 assert(s.commitBuildFile(partial?SECTION_FILE_PARTIAL_VERSION:84,71,97));
 s.partial_=partial;
 s.partialPageCount_=partial?2:0;
 s.pageCount=partial?9:2;
 const auto valid=Storage.files.at(s.filePath);
 for (int i : {1,0,1}) {
   auto page=s.loadPageAt(i);
   assert(page && page->marker=='A'+i && page->visibleTextOffset==3U+3U*i);
 }
 assert(!s.loadPageAt(-1) && !s.loadPageAt(2));
 fault={};
 assert(s.loadPageAt(1));
 const int reads=fault.reads, seeks=fault.seeks;
 for (int i=1;i<=reads;++i) {
   fault={}; fault.readAt=i;
   assert(!s.loadPageAt(1));
 }
 for (int i=0;i<seeks;++i) {
   fault={}; fault.seekAt=i;
   assert(!s.loadPageAt(1));
 }
 fault={};
 assert(s.loadPageAt(1));
 auto& bytes=Storage.files.at(s.filePath);
 auto put=[&](size_t at,uint32_t word) {std::memcpy(bytes.data()+at,&word,sizeof(word));};
 for (size_t slot=0;slot<5;++slot) {
   for (uint32_t invalid : {0U,UINT32_MAX,64U}) {
     if (!slot && invalid==64) continue; // The real page table begins at byte 64.
     put(HEADER_SIZE-20+slot*4,invalid);
     assert(!s.loadPageAt(1));
     bytes=valid;
   }
 }
 for (uint32_t invalid : {0U,45U,64U,UINT32_MAX}) {
   put(64+4,invalid);
   assert(!s.loadPageAt(1));
   bytes=valid;
 }
 bytes.resize(valid.size()-1);
 assert(!s.loadPageAt(0));
}
int main(){
 checkActivePageRead();checkCachedPages(false);checkCachedPages(true);
 auto seed=[](Section& s){Storage={};Storage.files[s.filePath]="old";Storage.files[s.binTmpPath()]=std::string(64,'\0');s.file.path=s.binTmpPath();};
 Section success;seed(success);assert(success.commitBuildFile(SECTION_FILE_PARTIAL_VERSION,100,200));
 const int writes=success.file.calls;assert(writes>10 && Storage.files[success.filePath]!="old" && !Storage.files.contains(success.filePath+".bak"));
 for(int at=0;at<writes;++at){Section s;seed(s);s.file.failAt=at;assert(!s.commitBuildFile(SECTION_FILE_PARTIAL_VERSION,100,200));assert(Storage.files[s.filePath]=="old");}
 for(int at=0;at<2;++at){Section s;seed(s);s.file.failSeek=at;assert(!s.commitBuildFile(SECTION_FILE_PARTIAL_VERSION,100,200));assert(Storage.files[s.filePath]=="old");}
 {Section s;seed(s);s.file.syncOk=false;assert(!s.commitBuildFile(SECTION_FILE_PARTIAL_VERSION,100,200));assert(Storage.files[s.filePath]=="old");}
 {Section s;seed(s);s.file.closeOk=false;assert(!s.commitBuildFile(SECTION_FILE_PARTIAL_VERSION,100,200));assert(Storage.files[s.filePath]=="old");}
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++','-std=c++20',str(path / 'test.cpp'),'-o',str(path / 'test')],check=True)
    subprocess.run([str(path / 'test')],check=True)
print('Section cache write, read, and recovery checks passed')
