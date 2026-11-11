# Daily reading durations

CrossInk v2.0.0-rc (`b8feaec4d`) stores daily cumulative seconds in
`/.crosspoint/daily_reading`. Its current monthly files contain 31 pairs of
32-bit total/uploaded counters, a month anchor, generation, and FNV-1a checksum.
The two monthly slots are 264 bytes each. Older daily records are 13 bytes.

## Built

- [x] Decode and encode the RC monthly format.
- [x] Decode the older daily format.
- [x] Validate checksums, counter bounds, month length, and zero entries beyond
  the end of a month.
- [x] Select the newest valid slot across generation rollover. Conflicting equal
  generations, ambiguous half-range generations, unreadable files, and unknown
  newer formats require recovery rather than silently choosing a copy.
- [x] Check a known wire-format checksum, every truncated monthly record, each
  single-byte corruption, legacy records, and slot-selection failures.

The codec has no storage calls or heap allocations. The store validates the
calendar anchor and month length, then runs each complete operation under
`Storage.withStorageLock`. One checked, short-lived allocation holds both month
copies, the 264-byte buffer, and transaction state; its size is capped at 832
bytes at compile time. This avoids putting that scratch on a reader task stack.
It does not add a task or resident cache.

## Remaining

- [x] Add checked storage reads and writes, preserving the last valid slot and
  original legacy files. Host fault checks pass; cumulative storage review remains.
- [x] Collect dated reading intervals through the existing timer, including
  pauses, the minimum-session threshold, and midnight boundaries. The recorder
  is gated by `CROSSPOINT_EXPERIMENTAL_DAILY_STATS`.
- [x] Checkpoint on reader pause/finish and retry accepted counters without
  counting them twice. Runtime flash latency and device behavior remain unverified.
- [x] Show recorded Today and the 7-day daily average in the Device view.
  Missing or unreadable days prevent a complete average; existing total time
  is never backfilled.
- [ ] Complete storage/recovery review and device testing.

C4 core behavior is built behind the daily-statistics flag; release validation remains.

## Storage checkpoint

Reads never create files. They merge the requested day's valid legacy primary,
backup, and temporary counters by maximum, leaving every original intact. Writes
merge all legacy days in the month before publishing an absolute counter.
Seconds and uploaded acknowledgments cannot decrease. Repeating the same value
after an uncertain sync or close does not add it again. The alternate slot is
closed and decoded again before a save reports success.

The store checks independent bounded file chains before overwriting an existing
slot, including its relationship to preserved legacy files. Empty interrupted
targets are supported. I/O failures and unknown newer formats block writes. A
damaged known-version slot can fall back to a valid sibling.

Bootstrap recovery follows the tagged RC writer's protocol: b is established
before a, and the writer never deletes a. A missing a plus a corrupt b therefore
identifies an unfinished first bootstrap under the supported failure model of
interrupted file writes. A new absolute write can reconstruct b from preserved
legacy counters, verify it, and then publish a. Read-only inspection still
reports that state unavailable. Foreign formats, I/O failures, aliasing with
legacy data, and a lone corrupt a block recovery. This does not repair external
file deletion, general filesystem damage, or lost uncheckpointed RAM data.

The initial storage checkpoint passed thirteen focused host checks, including all 264 incomplete update lengths,
uncertain sync/close retries, legacy preservation, aliasing, leap days, allocation
and lock failures, and first-bootstrap failure. The fake filesystem models these
API failures; actual FAT chains and device power-loss behavior remain unverified.

The optional recorder is connected to the existing page timer. The default
firmware leaves it disabled. Today and 7-day summaries are available in the enabled profile.

Checkpoint validation: the 13 focused checks passed in 0.60 seconds and the
default X4 Pro build passed in 74.26 seconds. The unconnected store's private
scratch type is not available in the linked image for a debugger size query;
the compile-time 832-byte limit is the current allocation bound.

## Optional recorder

`CROSSPOINT_EXPERIMENTAL_DAILY_STATS` adds a fixed, reader-owned session buffer
capped at 256 bytes. It creates no task, new timer, or heap allocation. Eligible
page dwell follows the existing idle cutoff and pauses. Local clock capture at
the start of a page assigns its time to dates, splitting at midnight. Unknown
start dates do not produce daily counters or backfill prior history. Clock
corrections follow the existing dated page-timer behavior; no independent UTC
interval validator is introduced in this stage.

The 10-second minimum applies across the current tracking session. Short dated
intervals wait in RAM until that floor is reached. A finished sub-floor session
discards them. Accepted entries survive the normal statistics commit until the
daily writer confirms them. A retry keeps its prepared absolute target, so an
uncertain previous save cannot add the same seconds twice. Pauses flush the
buffer; drawing and page-timer delivery do not read or write daily files.

At most ten distinct dates can wait in the buffer. Exceeding that limit reports
failure while still trying to save the retained dates. The buffer is owned by
the open reader: failed values can retry while it remains alive; a power loss
or reader destruction can lose uncheckpointed values. Save failures feed the
existing snapshot/finish result rather than silently reporting all statistics
saved. Book and global saves are still attempted when the daily save fails.

Default and enabled host builds keep the original statistics tests. The enabled
variant adds recorder integration cases for menu pauses, idle cutoff, short
sessions, midnight splitting, and an uncertain save followed by retry.

The enabled X4 Pro image reports a 244-byte daily session buffer and a 700-byte
`ReaderStats` object. Snapshot previews use a const projection of pending totals
instead of copying the 412-byte combined timer/session object onto the stack.
Repeated previews are checked to leave the live timer and counters untouched.
The enabled validation profile also includes completion, completion prompting,
and statistics restore; it is not an installation candidate.

Recorder checkpoint validation: 410 focused default/enabled checks passed in
0.67 seconds. The cumulative 2,227-case host run passed 2,226 checks in 47.90 s;
one older Saved Items fixture lacked the Home sizing helper added in the
Carousel stage. Its fixture-only correction passed in a 0.65-second rerun.
The final enabled X4 Pro build passed in 80.00 seconds. The first 178.74-second
build preceded the stack-copy correction and is not the final candidate.
Physical timing, daily-file durability, first-bootstrap recovery, and review
remain open; no firmware was installed.

## Device summary

The Device tab shows Recorded today and Recorded 7-day average. The summary
loads once on entry; rendering and navigation do not reopen its files. An
unreadable daily source contributes a Partial data warning, combined with an
unsaved-data warning when both apply. Missing history or an unset clock shows a
dash. A zero stored counter displays zero; the average divides seven stored
calendar-day values, including stored zeros. A missing month does not become
seven invented zero-reading days. These are recorded durations, not a
reconstruction of untracked reading. All Devices retains its aggregate rows
and does not present local daily values as an aggregate.

Recovery checks cover every 0–263-byte interruption of both bootstrap and normal
updates, preserve legacy files, and retry empty targets through the existing
checked-chain HAL contract. Five summary cases cover missing history, stored
zeros, month boundaries, unreadable data, and overflow-safe accumulation. UI
checks cover enabled/disabled recording, restore combinations, warnings, missing
clock data, and row routing. Actual FAT failure behavior and e-ink acceptance
remain part of the cumulative release review and device test.

Summary checkpoint: all 406 relevant checks passed in 3.05 seconds; the enabled
X4 Pro build passed in 111.61 seconds. No firmware was installed.
