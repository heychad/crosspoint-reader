#include "CompletionJournal.h"
#include "CompletionTransaction.h"
namespace readingstats {
CompletionProbeResult probeClosedBookCompletion(const std::string&, bool& target, uint8_t* policy) {
  target = true;
  if (policy) *policy = completionjournal::MOVE_TO_READ | completionjournal::MOVE_FROM_DEFERRED;
  return CompletionProbeResult::Matching;
}
}  // namespace readingstats
