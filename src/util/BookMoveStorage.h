#pragma once

#include "BookMove.h"
#include "BookMovePaths.h"

namespace bookmove {
inline constexpr char JOURNAL_ROOT[] = "/.book-moves";
inline constexpr char CURRENT[] = "/.book-moves/current";
inline constexpr char PLAN[] = "/.book-moves/current/plan.bin";
inline constexpr char SEAL[] = "/.book-moves/current/seal.bin";

// One checked heap owner per synchronous operation. Strings are cold-I/O
// paths; no book contents or directory listings are held in memory.
struct Work {
  PlanBytes bytes{};
  Identities identities{};
  size_t size = 0;
  uint64_t id = 0;
  std::string from, to, archive;
  BookKind kind = BookKind::Reflowable;
};

bool bookKind(const std::string& from, const std::string& to, BookKind& kind);
Result prepare(Work& work);
Result inspect(Work& work);
Result execute(Work& work);
// Implemented by the application coordinator. Called under render/storage
// locks, before mutation and again before retiring a completed transaction.
bool destinationReferencesClear(const Work& work);
bool updateReferences(const Work& work);
}  // namespace bookmove
