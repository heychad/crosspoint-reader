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

The codec has no storage calls, heap allocations, or runtime hooks. The caller
must validate the calendar anchor and supply the month's length. It needs one
264-byte encoding buffer and a 252-byte decoded month; the runtime owner and
storage-lock boundaries remain to be implemented.

## Remaining

- [ ] Add checked storage reads and writes, preserving the last valid slot and
  original legacy files.
- [ ] Collect dated reading intervals through the existing timer, including
  pauses, the minimum-session threshold, and midnight boundaries.
- [ ] Checkpoint and retry accepted counters without counting them twice.
- [ ] Show Today and the 7-day daily average; keep unavailable dates distinct
  from known zero-reading days. Existing total time cannot be backfilled.
- [ ] Complete storage/recovery review and device testing.

No new counters are recorded by the current firmware, and existing statistics
files remain unchanged. This is an implementation checkpoint, not completed C4.
