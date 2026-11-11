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
- [ ] Collect dated reading intervals through the existing timer, including
  pauses, the minimum-session threshold, and midnight boundaries.
- [ ] Checkpoint and retry accepted counters without counting them twice.
- [ ] Show Today and the 7-day daily average; keep unavailable dates distinct
  from known zero-reading days. Existing total time cannot be backfilled.
- [ ] Complete storage/recovery review and device testing.

C4 remains in progress.

## Storage checkpoint

Reads never create files. They merge the requested day's valid legacy primary,
backup, and temporary counters by maximum, leaving every original intact. Writes
merge all legacy days in the month before publishing an absolute counter.
Seconds and uploaded acknowledgments cannot decrease. Repeating the same value
after an uncertain sync or close does not add it again. The alternate slot is
closed and decoded again before a save reports success.

The store checks independent small-file clusters before overwriting an existing
slot, including its relationship to preserved legacy files. I/O failures and
unknown newer formats block writes. A damaged known-version slot can fall back
to a valid sibling. A torn first bootstrap with no valid copy remains unavailable
and keeps its evidence for recovery; it is not silently reinitialized. This
limitation must be addressed or accepted at the storage release review.

Thirteen focused host checks pass, including all 264 incomplete update lengths,
uncertain sync/close retries, legacy preservation, aliasing, leap days, allocation
and lock failures, and first-bootstrap failure. The fake filesystem models these
API failures; actual FAT chains and device power-loss behavior remain unverified.

The store is not yet connected to the reading timer, so the current firmware
still does not record daily counters. Today and 7-day summaries remain pending.

Checkpoint validation: the 13 focused checks passed in 0.60 seconds and the
default X4 Pro build passed in 74.26 seconds. The unconnected store's private
scratch type is not available in the linked image for a debugger size query;
the compile-time 832-byte limit is the current allocation bound.
