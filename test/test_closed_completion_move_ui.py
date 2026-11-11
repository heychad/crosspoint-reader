"""Exercise the Statistics screen's experimental closed-book move routing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/activities/settings/ReadingStatsActivity.cpp').read_text()
start = source.index('readingstats::ClosedCompletionResult attemptCompletion(')
opening = source.index('{', start)
depth = 0
for index in range(opening, len(source)):
    depth += (source[index] == '{') - (source[index] == '}')
    if depth == 0:
        method = source[start:index + 1]
        break
else:
    raise RuntimeError('attemptCompletion not found')
code = r'''
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include "util/ReadFolder.h"
#define CROSSPOINT_EXPERIMENTAL_COMPLETION_MOVE 1
struct ReadingStatsDate {};
struct Settings {bool removeReadBooksFromRecents=false,moveFinishedToReadFolder=true;} SETTINGS;
namespace FsHelpers {bool hasEpubExtension(const std::string& path){return path.ends_with(".epub");}}
struct HalStorage {enum class PathStatus {Missing,Present,Failed};PathStatus status=PathStatus::Missing;
 PathStatus lookupPath(const char*){return status;}} Storage;
struct Recents {bool ok=true;int calls=0;bool removeByPathPersisted(const std::string&){++calls;return ok;}} RECENT_BOOKS;
namespace readingstats {
namespace completionjournal {constexpr uint8_t REMOVE_RECENTS=1,MOVE_TO_READ=2;}
enum class ClosedCompletionResult {Applied,Unchanged,Invalid,Busy,Unavailable,RecoveryRequired};
enum class CompletionProbeResult {Matching,NoPending,DifferentTransaction,ManualReview,Unavailable};
enum class CompletionRecoveryResult {Recovered,NoPending,Busy,DifferentTransaction,ManualReview,Unavailable};
enum class CompletionFinalizeResult {Finalized,NoPending,Busy,DifferentTransaction,ManualReview,Unavailable};
int writes=0,recoveries=0,finalizations=0;uint8_t writtenPolicy=0,recordedPolicy=0;bool recordedTarget=true;
CompletionProbeResult probe=CompletionProbeResult::NoPending;
CompletionRecoveryResult recovery=CompletionRecoveryResult::NoPending;
ClosedCompletionResult setClosedBookCompletionStats(const std::string&,bool,const ReadingStatsDate&,uint8_t policy){++writes;writtenPolicy=policy;return ClosedCompletionResult::Applied;}
CompletionProbeResult probeClosedBookCompletion(const std::string&,bool& target,uint8_t* policy){target=recordedTarget;*policy=recordedPolicy;return probe;}
CompletionRecoveryResult recoverClosedBookCompletionStats(const std::string&,bool){++recoveries;return recovery;}
CompletionFinalizeResult finalizeClosedBookCompletion(const std::string&,bool){++finalizations;return CompletionFinalizeResult::Finalized;}
namespace completionmove {
enum class PlanResult {Ready,Conflict};enum class ReferenceResult {Updated,RecoveryRequired};
PlanResult plan=PlanResult::Ready;ReferenceResult references=ReferenceResult::Updated;int plans=0,moves=0;std::string destination;
PlanResult preparePlan(const std::string&,const std::string& to){++plans;destination=to;return plan;}
ReferenceResult updateMoveReferences(){++moves;return references;}
}
}
''' + method + r'''
int main(){
 using namespace readingstats;using namespace readingstats::completionmove;
 const std::string source="/Authors/Writer/Book.epub";ReadingStatsDate today;std::string moved;
 assert(attemptCompletion(source,true,today,false,&moved)==ClosedCompletionResult::Applied);
 assert(moved=="/read/Book.epub" && destination==moved && writes==1 && writtenPolicy==2 && plans==1 && moves==1 && finalizations==0);
 Storage.status=HalStorage::PathStatus::Present;
 assert(attemptCompletion(source,true,today,false,&moved)==ClosedCompletionResult::Unavailable && writes==1);
 Storage.status=HalStorage::PathStatus::Missing;
 assert(attemptCompletion(source,true,today,false,nullptr,false)==ClosedCompletionResult::Unavailable && writes==1);
 plan=PlanResult::Conflict;
 assert(attemptCompletion(source,true,today,false,&moved)==ClosedCompletionResult::RecoveryRequired && writes==2);
 plan=PlanResult::Ready;references=ReferenceResult::RecoveryRequired;
 assert(attemptCompletion(source,true,today,false,&moved)==ClosedCompletionResult::RecoveryRequired && writes==3);
 references=ReferenceResult::Updated;SETTINGS.moveFinishedToReadFolder=false;
 probe=CompletionProbeResult::Matching;recordedPolicy=2;recovery=CompletionRecoveryResult::Recovered;
 assert(attemptCompletion(source,true,today,true,&moved)==ClosedCompletionResult::Unchanged);
 assert(destination=="/read/Book.epub" && moved==destination && writes==3 && recoveries==1);
 assert(attemptCompletion(source,true,today,true,nullptr,false)==ClosedCompletionResult::Unavailable && recoveries==1);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['c++', '-std=c++20', '-I', str(root / 'src'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
print('Closed-book move routing and retry checks passed')
