#pragma once

struct BookReadingStats;
struct GlobalReadingStats;
struct ReadingStatsDate;

namespace readingstats {
enum class CompletionChange { Invalid, Unchanged, Changed };

// Updates snapshots only. The caller owns durable, paired publication and any moves.
CompletionChange applyCompletion(BookReadingStats& book, GlobalReadingStats& global, bool completed,
                                 const ReadingStatsDate& today);
}  // namespace readingstats
