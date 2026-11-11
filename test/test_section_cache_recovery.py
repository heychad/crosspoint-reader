"""Exercise production cache publication and recovery with injected storage failures."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'lib/Epub/Epub/Section.cpp').read_text()
def extract(name, return_type='bool'):
    start = source.index(return_type + ' ' + name + '(')
    opening = source.index('{', start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if not depth:
            return source[start:index + 1]
    raise RuntimeError(name + ' not found')
code = r'''
#include <cassert>
#include <map>
#include <set>
#include <string>
#define LOG_ERR(...) ((void)0)
#define LOG_DBG(...) ((void)0)
constexpr size_t HEADER_SIZE=3;
struct HalFile {size_t size=0;size_t fileSize()const{return size;}bool close(){return true;}};
struct HalStorage {
 enum class PathStatus {Missing,Present,Failed};
 std::map<std::string,std::string> files;
 bool independent=false;
 std::set<std::string> failedLookup, failedRemove;
 std::set<std::pair<std::string,std::string>> failedRename;
 PathStatus lookupPath(const char* p){if(failedLookup.contains(p))return PathStatus::Failed;return files.contains(p)?PathStatus::Present:PathStatus::Missing;}
 bool openFileForRead(const char*,const std::string& p,HalFile& f){if(!files.contains(p))return false;f.size=files[p].size();return true;}
 bool distinctMoveChains(const char*,const char*){return independent;}
 bool rename(const char* from,const char* to){if(failedRename.contains({from,to}) || !files.contains(from) || files.contains(to))return false; files[to]=files[from];files.erase(from);return true;}
 bool remove(const char* p){if(failedRemove.contains(p))return false;return files.erase(p)>0;}
} Storage;
struct Section {std::string filePath="/section.bin";std::string binTmpPath()const{return filePath+".part";}bool clearCache()const;};
''' + extract('recoverSectionCache') + '\n' + extract('promoteSectionCache') + '\n' + extract('retireSectionCacheBackup','void') + '\n' + extract('Section::clearCache') + r'''
int main(){
 const std::string path="/section.bin", tmp=path+".part", bak=path+".bak";
 auto seed=[&]{Storage={};Storage.files[path]="old";Storage.files[tmp]="new";};
 seed(); assert(promoteSectionCache(tmp,path)); assert(Storage.files[path]=="new" && !Storage.files.contains(bak));
 seed(); Storage.failedRename.insert({path,bak}); assert(!promoteSectionCache(tmp,path)); assert(Storage.files[path]=="old" && Storage.files[tmp]=="new");
 seed(); Storage.failedRename.insert({tmp,path}); assert(!promoteSectionCache(tmp,path)); assert(Storage.files[path]=="old" && Storage.files[tmp]=="new");
 seed(); Storage.failedRename.insert({tmp,path}); Storage.failedRename.insert({bak,path});
 assert(!promoteSectionCache(tmp,path)); assert(!Storage.files.contains(path) && Storage.files[bak]=="old");
 Storage.failedRename.clear(); assert(recoverSectionCache(path)); assert(Storage.files[path]=="old" && !Storage.files.contains(bak));
 seed(); Storage.files[bak]="older"; assert(!promoteSectionCache(tmp,path)); assert(Storage.files[path]=="old" && Storage.files[bak]=="older");
 seed(); Storage.failedRemove.insert(bak); assert(promoteSectionCache(tmp,path)); assert(Storage.files[path]=="new" && Storage.files[bak]=="old");
 Storage.files[tmp]="next"; assert(!promoteSectionCache(tmp,path)); assert(Storage.files[path]=="new" && Storage.files[bak]=="old");
 seed(); Storage.failedLookup.insert(bak); assert(!promoteSectionCache(tmp,path)); assert(Storage.files[path]=="old");
 Storage={}; Storage.files[bak]="old"; assert(recoverSectionCache(path)); assert(Storage.files[path]=="old");
 Storage={}; Storage.files[path]=""; Storage.files[bak]="old"; assert(recoverSectionCache(path));
 assert(Storage.files[path]=="old" && Storage.files[path+".bad-0"].empty() && !Storage.files.contains(bak));
 Storage={}; Storage.files[path]=""; Storage.files[bak]="old"; Storage.files[path+".bad-0"]="keep";
 assert(recoverSectionCache(path)); assert(Storage.files[path]=="old" && Storage.files[path+".bad-0"]=="keep" && Storage.files.contains(path+".bad-1"));
 Storage={}; Storage.files[path]=""; Storage.files[bak]="old";
 for(int i=0;i<8;++i)Storage.files[path+".bad-"+std::to_string(i)]="keep";
 assert(!recoverSectionCache(path)); assert(Storage.files[bak]=="old" && Storage.files[path+".bad-0"]=="keep");
 Section section;Storage={};Storage.files[path]="broken";Storage.files[bak]="old";Storage.files[tmp]="stage";
 assert(!section.clearCache());assert(Storage.files[path]=="broken" && Storage.files[bak]=="old" && Storage.files[tmp]=="stage");
 Storage.files.erase(bak);assert(section.clearCache());assert(!Storage.files.contains(path) && !Storage.files.contains(tmp));
 seed(); Storage.files[bak]="old"; retireSectionCacheBackup(path); assert(Storage.files.contains(bak));
 Storage.independent=true; retireSectionCacheBackup(path); assert(!Storage.files.contains(bak) && Storage.files[path]=="old");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Section cache promotion and recovery checks passed')
