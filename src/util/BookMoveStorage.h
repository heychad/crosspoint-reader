#pragma once

#include <optional>

#include "BookMove.h"
#include "BookMovePaths.h"

namespace bookmove {
inline constexpr char JOURNAL_ROOT[] = "/.book-moves";
inline constexpr char CURRENT[] = "/.book-moves/current";
inline constexpr char PLAN[] = "/.book-moves/current/plan.bin";
inline constexpr char SEAL[] = "/.book-moves/current/seal.bin";

enum class Operation { Move, Delete };

// One checked heap owner per synchronous operation. Strings are cold-I/O
// paths; no book contents or directory listings are held in memory.
struct Work {
  PlanBytes bytes{};
  Identities identities{};
  size_t size = 0;
  uint64_t id = 0;
  std::string from, to, archive;
  BookKind kind = BookKind::Reflowable;
  Operation operation = Operation::Move;
};

bool bookKind(const std::string& from, const std::string& to, BookKind& kind);
Result prepare(Work& work);
Result inspect(Work& work);
Result execute(Work& work);
// Caller holds the storage lock across validation and any removal of the payload.
bool verifyCommittedDeletion(const std::string& held, const std::string& original,
                             std::optional<uint64_t> fingerprint = std::nullopt);
// Implemented by the application coordinator. Called under render/storage
// locks, before mutation and again before retiring a completed transaction.
bool destinationReferencesClear(const Work& work);
bool updateReferences(const Work& work);
}  // namespace bookmove
