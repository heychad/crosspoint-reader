"""Exercise the production post-reader move dispatcher with controlled outcomes."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'src/activities/reader/DeferredCompletionExecutor.cpp').read_text()
body = source[source.index('namespace readingstats::deferredcompletion {'):]
program = r'''
#include <cassert>
#include <memory>
#include <string>
#include "activities/reader/DeferredCompletionMove.h"
#include "activities/reader/DeferredCompletionIntentIO.h"
#include "activities/reader/CompletionMovePlan.h"
#include "activities/reader/CompletionMoveReferences.h"
#include "util/ReadFolder.h"
struct ReadingStatsDate {};
struct Manager { bool main = true; bool isMainTask() const { return main; } } activityManager;
bool failAllocation = false;
template<class T> std::unique_ptr<T> makeUniqueNoThrow() {
  return failAllocation ? nullptr : std::make_unique<T>();
}
namespace dc = readingstats::deferredcompletion;
namespace cm = readingstats::completionmove;
dc::PrepareMoveResult prepared = dc::PrepareMoveResult::Prepared;
cm::ReferenceResult reference = cm::ReferenceResult::Updated;
cm::DeferredRecoveryResult recovery = cm::DeferredRecoveryResult::NoPending;
int prepares = 0, moves = 0;
dc::FindResult discovery = dc::FindResult::Found;
std::string capturedSource, capturedDestination;
dc::PrepareMoveResult dc::prepareMove(const std::string& source, const std::string& destination,
                                       const ReadingStatsDate&) {
  ++prepares; capturedSource = source; capturedDestination = destination; return prepared;
}
cm::ReferenceResult cm::updateMoveReferences() { ++moves; return reference; }
cm::DeferredRecoveryResult cm::recoverDeferredCompletion() { return recovery; }
dc::FindResult dc::discoverResumable(Record& record, size_t& size, unsigned& slot) {
  if (discovery == FindResult::Found) {
    assert(encode("/Authors/A/book.epub", 123, record, size));
    slot = 0;
  }
  return discovery;
}
''' + body + r'''
int main() {
  ReadingStatsDate date;
  using E = dc::ExecuteMoveResult;
  activityManager.main = false;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::Unavailable);
  assert(prepares == 0 && moves == 0);
  activityManager.main = true;
  for (auto path : {"relative.epub", "/read/book.epub", "/READ/book.epub", "/a/../b"}) {
    assert(dc::executeAfterReaderClosed(path, date) == E::Blocked);
  }
  assert(prepares == 0 && moves == 0);
  prepared = dc::PrepareMoveResult::Busy;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::Busy);
  assert(capturedSource == "/Authors/A/book.epub" && capturedDestination == "/read/book.epub");
  assert(moves == 0);
  prepared = dc::PrepareMoveResult::Cancelled;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::Cancelled && moves == 0);
  prepared = dc::PrepareMoveResult::Missing;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::Missing && moves == 0);
  prepared = dc::PrepareMoveResult::Prepared;
  reference = cm::ReferenceResult::Unavailable;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::Unavailable);
  reference = cm::ReferenceResult::NoPlan;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::RecoveryRequired);
  reference = cm::ReferenceResult::Updated;
  assert(dc::executeAfterReaderClosed("/Authors/A/book.epub", date) == E::Moved);
  const int priorPrepares = prepares, priorMoves = moves;
  discovery = dc::FindResult::Missing;
  assert(dc::executeOnePending(date) == E::Missing && prepares == priorPrepares && moves == priorMoves);
  discovery = dc::FindResult::Blocked;
  assert(dc::executeOnePending(date) == E::RecoveryRequired && prepares == priorPrepares && moves == priorMoves);
  failAllocation = true;
  assert(dc::executeOnePending(date) == E::Unavailable && prepares == priorPrepares && moves == priorMoves);
  failAllocation = false;
  discovery = dc::FindResult::Found;
  bool shown = false;
  const auto show = [](void* context) { *static_cast<bool*>(context) = true; };
  assert(dc::executeOnePending(date, show, &shown) == E::Moved && shown);
  assert(prepares == priorPrepares + 1 && moves == priorMoves + 1);
  shown = false;
  recovery = cm::DeferredRecoveryResult::RecoveryRequired;
  assert(dc::executeOnePending(date, show, &shown) == E::RecoveryRequired && !shown);
  assert(prepares == priorPrepares + 1 && moves == priorMoves + 1);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    src = Path(temporary) / 'test.cpp'
    binary = Path(temporary) / 'test'
    src.write_text(program)
    subprocess.run(['c++', '-std=c++20', '-I', str(ROOT / 'src'), str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Deferred completion executor dispatch passed')
