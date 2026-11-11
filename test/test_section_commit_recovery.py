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
constexpr uint8_t SECTION_FILE_PARTIAL_VERSION=99;
constexpr size_t HEADER_SIZE=32;
struct HalStorage {
 enum class PathStatus{Missing,Present,Failed};std::map<std::string,std::string> files;
 PathStatus lookupPath(const char* p){return files.contains(p)?PathStatus::Present:PathStatus::Missing;}
 bool rename(const char* a,const char* b){if(!files.contains(a)||files.contains(b))return false;files[b]=files[a];files.erase(a);return true;}
 bool remove(const char* p){return files.erase(p)>0;}
 bool openFileForRead(const char*,const std::string&,struct HalFile&);
} Storage;
struct HalFile {
 std::string path; size_t pos=64;int calls=0, failAt=-1, seeks=0, failSeek=-1;bool syncOk=true,closeOk=true;
 size_t position()const{return pos;}
 bool seek(size_t n){if(seeks++==failSeek)return false;pos=n;return true;}
 size_t write(const uint8_t* data,size_t n){if(calls++==failAt)return 0;auto& s=Storage.files[path];if(s.size()<pos+n)s.resize(pos+n);s.replace(pos,n,reinterpret_cast<const char*>(data),n);pos+=n;return n;}
 bool sync(){return syncOk;}bool close(){return closeOk;}
 size_t fileSize()const{return Storage.files[path].size();}
};
bool HalStorage::openFileForRead(const char*,const std::string& p,HalFile& f){if(!files.contains(p))return false;f.path=p;return true;}
namespace serialization {
 template<class T>bool tryWritePod(HalFile& f,const T& t){return f.write(reinterpret_cast<const uint8_t*>(&t),sizeof(T))==sizeof(T);}
 bool tryWriteString(HalFile& f,const std::string& s){uint32_t n=s.size();return tryWritePod(f,n) && (!n || f.write(reinterpret_cast<const uint8_t*>(s.data()),n)==n);}
}
struct Parser {std::vector<std::pair<std::string,uint16_t>> anchors{{"part",0}};const auto& getAnchors(){return anchors;}};
struct Entry{uint32_t fileOffset;uint16_t paragraphIndex,listItemIndex;uint32_t visibleTextOffset;};
struct Build{std::vector<Entry> lut{{40,1,2,3},{50,4,5,6}};std::unique_ptr<Parser> parser=std::make_unique<Parser>();};
struct Section {HalFile file;std::string filePath="/chapter";std::unique_ptr<Build> build_=std::make_unique<Build>();uint16_t builtPageCount_=2;
 std::string binTmpPath()const{return filePath+".part";}bool commitBuildFile(uint8_t,uint32_t,uint32_t);};
''' + extract('bool recoverSectionCache(') + '\n' + extract('bool promoteSectionCache(') + '\n' + extract('bool Section::commitBuildFile(') + r'''
int main(){
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
print('Section commit short-write, seek, sync and close checks passed')
