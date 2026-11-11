"""Exercise move-reference sequencing and retry using the production function."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/activities/reader/CompletionMoveReferences.cpp").read_text()
start = source.index("namespace {\nReferenceResult updateMoveReferencesLocked()")
end = source.index("\n}  // namespace readingstats::completionmove", start)
method = source[start:end]

code = r'''
#include <cassert>
#include <cstdint>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
template<class T> std::unique_ptr<T> makeUniqueNoThrow(){return std::make_unique<T>();}
bool underStorageLock=false;
bool underRenderLock=false, allowRenderLock=true;
struct Manager {bool mainTask=true;bool isMainTask() const{return mainTask;}} activityManager;
class RenderLock {
 public:
 enum class Mode {Try};
 explicit RenderLock(Mode){if(allowRenderLock){assert(!underRenderLock);underRenderLock=true;held=true;}}
 ~RenderLock(){if(held)underRenderLock=false;}
 bool ownsLock() const{return held;}
 private: bool held=false;
};
namespace readingstats {
namespace completionjournal {
constexpr uint8_t MOVE_TO_READ=2,REMOVE_RECENTS=1,MOVE_FROM_DEFERRED=8;
enum class RetirementResult {NoCandidate,Retired,NotApplied,Conflict,Blocked,Unavailable,Uncertain};
RetirementResult retirement=RetirementResult::Retired;
int calls=0;
RetirementResult retire(){assert(underStorageLock && underRenderLock);++calls;return retirement;}
}
namespace deferredcompletion {
enum class RetireMoveResult {Retired,AlreadyRetired,Missing,Busy,RecoveryRequired,Unavailable};
RetireMoveResult result=RetireMoveResult::Retired;
int calls=0;
RetireMoveResult retireMovedIntent(const std::string& source){assert(underStorageLock && source=="/old.epub");++calls;return result;}
}
enum class CompletionProbeResult {Matching,NoPending};
uint8_t savedPolicy=2;
CompletionProbeResult probeClosedBookCompletion(const std::string&,bool& target,uint8_t* policy){
 target=true;*policy=savedPolicy;return CompletionProbeResult::Matching;
}
namespace completionmove {
enum class ReferenceResult {Updated,NoPlan,Conflict,Unavailable,RecoveryRequired};
enum class MoveFilesResult {Moved,NoPlan,Conflict,Unavailable,RecoveryRequired};
enum class VerifyFilesResult {ContentMatches,NoPlan,NotReady,Unavailable};
VerifyFilesResult verifyResult=VerifyFilesResult::ContentMatches;int verifyCalls=0;
VerifyFilesResult verifyPreparedFiles(){assert(underStorageLock);++verifyCalls;return verifyResult;}
enum class PlanResult {Ready,Missing};
struct Plan {};
MoveFilesResult moveResult=MoveFilesResult::Moved;int moveCalls=0;
MoveFilesResult movePreparedFiles(){assert(underStorageLock);++moveCalls;return moveResult;}
PlanResult inspectPlan(Plan&,size_t&,std::string_view& from,std::string_view& to){
 from="/old.epub";to="/read/book.epub";return PlanResult::Ready;
}
}
}
struct Recent {
 bool ok=true;int removed=0,updated=0;
 struct Book {std::string path;};
 std::vector<Book> books;
 const std::vector<Book>& getBooks() const {assert(underRenderLock && underStorageLock);return books;}
 bool removeByPathPersisted(const std::string& path){assert(underStorageLock && path=="/old.epub");++removed;return ok;}
 bool updatePathPersisted(const std::string& old,const std::string& next,const std::string& a,const std::string& b){
 assert(underStorageLock && old=="/old.epub" && next=="/read/book.epub" && a==b);++updated;return ok;
 }
} RECENT_BOOKS;
struct App {
 bool ok=true;int updates=0;
 std::string openEpubPath;
 bool updateOpenEpubPathPersisted(const std::string& old,const std::string& next){
 assert(underStorageLock && old=="/old.epub" && next=="/read/book.epub");++updates;return ok;
 }
} APP_STATE;
struct StorageStub {
 bool allow=true;
 bool withStorageLock(bool (*operation)(void*),void* context){
  if(!allow)return false;
  assert(underRenderLock && !underStorageLock);underStorageLock=true;
  bool result=operation(context);underStorageLock=false;return result;
 }
} Storage;
namespace syncpublish {
enum class ReferenceGuardResult {Clear,Conflict,Unavailable};
ReferenceGuardResult result=ReferenceGuardResult::Clear;
bool failOnSecond=false;int guardCalls=0;
bool referenceMayReferTo(std::string_view reference,std::string_view source){
 if(reference.size()!=source.size())return false;
 for(size_t i=0;i<reference.size();++i)
  if(std::tolower(static_cast<unsigned char>(reference[i]))!=std::tolower(static_cast<unsigned char>(source[i])))return false;
 return true;
}
ReferenceGuardResult inspectStoredSourceReferences(StorageStub&,std::string_view source,bool (*tick)(void*),void* context){
 assert(underStorageLock && source=="/old.epub" && tick(context));
 ++guardCalls;return failOnSecond && guardCalls==2?ReferenceGuardResult::Conflict:result;
}
}
namespace readingstats::completionmove {
''' + method + r'''
}
int main(){
 using namespace readingstats;using namespace readingstats::completionmove;
 activityManager.mainTask=false;
 assert(updateMoveReferences()==ReferenceResult::Unavailable && moveCalls==0 && !underRenderLock);
 activityManager.mainTask=true;
 allowRenderLock=false;
 assert(updateMoveReferences()==ReferenceResult::Unavailable && moveCalls==0);
 allowRenderLock=true;
 Storage.allow=false;
 assert(updateMoveReferences()==ReferenceResult::Unavailable && moveCalls==0);
 Storage.allow=true;
 moveResult=MoveFilesResult::RecoveryRequired;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && RECENT_BOOKS.updated==0 && APP_STATE.updates==0);
 moveResult=MoveFilesResult::Moved;
 verifyResult=VerifyFilesResult::NotReady;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && RECENT_BOOKS.updated==0 && APP_STATE.updates==0);
 verifyResult=VerifyFilesResult::Unavailable;
 assert(updateMoveReferences()==ReferenceResult::Unavailable && RECENT_BOOKS.updated==0 && APP_STATE.updates==0);
 verifyResult=VerifyFilesResult::NoPlan;
 assert(updateMoveReferences()==ReferenceResult::NoPlan && RECENT_BOOKS.updated==0 && APP_STATE.updates==0);
 verifyResult=VerifyFilesResult::ContentMatches;RECENT_BOOKS.ok=false;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && RECENT_BOOKS.updated==1 && APP_STATE.updates==0);
 RECENT_BOOKS.ok=true;APP_STATE.ok=false;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && RECENT_BOOKS.updated==2 && APP_STATE.updates==1);
 APP_STATE.ok=true;
 syncpublish::result=syncpublish::ReferenceGuardResult::Conflict;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && RECENT_BOOKS.updated==3 && APP_STATE.updates==2);
 syncpublish::result=syncpublish::ReferenceGuardResult::Clear;
 assert(updateMoveReferences()==ReferenceResult::Updated && RECENT_BOOKS.updated==4 && APP_STATE.updates==3);
 savedPolicy=3;
 assert(updateMoveReferences()==ReferenceResult::Updated && RECENT_BOOKS.removed==1 && APP_STATE.updates==4);
 savedPolicy=10;
 APP_STATE.openEpubPath="/OLD.EPUB";
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && deferredcompletion::calls==0);
 APP_STATE.openEpubPath.clear();
 RECENT_BOOKS.books.push_back({"/OLD.EPUB"});
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && deferredcompletion::calls==0);
 RECENT_BOOKS.books.clear();
 deferredcompletion::result=deferredcompletion::RetireMoveResult::RecoveryRequired;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && deferredcompletion::calls==1);
 deferredcompletion::result=deferredcompletion::RetireMoveResult::Unavailable;
 assert(updateMoveReferences()==ReferenceResult::Unavailable && deferredcompletion::calls==2);
 deferredcompletion::result=deferredcompletion::RetireMoveResult::Retired;
 assert(updateMoveReferences()==ReferenceResult::Updated && deferredcompletion::calls==3);
 deferredcompletion::result=deferredcompletion::RetireMoveResult::AlreadyRetired;
 assert(updateMoveReferences()==ReferenceResult::Updated && deferredcompletion::calls==4);
 syncpublish::guardCalls=0;syncpublish::failOnSecond=true;
 const int beforeRetirement=completionjournal::calls;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired && completionjournal::calls==beforeRetirement);
 syncpublish::failOnSecond=false;
 completionjournal::retirement=completionjournal::RetirementResult::Uncertain;
 assert(updateMoveReferences()==ReferenceResult::RecoveryRequired);
 completionjournal::retirement=completionjournal::RetirementResult::Unavailable;
 assert(updateMoveReferences()==ReferenceResult::Unavailable);
 completionjournal::retirement=completionjournal::RetirementResult::Retired;
 assert(updateMoveReferences()==ReferenceResult::Updated);
}
'''
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    (path / "test.cpp").write_text(code)
    subprocess.run(["c++", "-std=c++20", str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
print("Completion move-reference sequencing and retry checks passed")
