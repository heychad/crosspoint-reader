# Reading statistics port — implementation checkpoint

This feature group adapts CrossInk v1.6.1 statistics to CrossPoint 2.0.0.
This file records its individual implementation and checks; see INTEGRATION.md
for the combined bars/controls/statistics candidate. It does not complete the
full CrossInk parity goal and has not been physically tested.

The chad.28 candidate extends the saved screen with book/device/aggregate views
and reading-pattern breakdowns. See [reading statistics views](READING_INSIGHTS.md).

## Implemented behavior

- Settings > Reader has a tracking switch (on by default, as in CrossInk), a
  30–600 second idle limit, and a read-only Reading statistics screen.
- EPUB (including the existing text/Markdown conversion path) and XTC share
  reader-owned tracking. Time is estimated from page dwell. A page interval
  beyond the idle limit contributes nothing. Page-turn and end-screen refresh time, child
  activities, inline overlays and the end-of-book screen are excluded. A manual
  same-page refresh stays within the current page-dwell estimate.
- Forward turns need at least two seconds of dwell to count a page/pace sample.
  Backward navigation contributes reading time but not forward-page pace.
- At least ten accumulated seconds add session time. At least sixty seconds
  count one session. Without an RTC, time and page counts still work; calendar
  buckets and streaks remain unavailable. Valid local dates use CrossPoint's
  timezone/DST-aware HAL, not a second offset calculation.
- The session retains small, fixed calendar buckets so a long menu pause does
  not attribute the next morning's reading to the previous night.
- Normal exit and sleep save accumulated statistics. A quick open/close that
  accumulated no time does not write statistics. Abrupt power loss can lose the
  current unsaved session.

## Data and compatibility

CrossInk book formats v1–v5 and global formats v1–v3 are read. New writes retain
book v5 (73 bytes) and global v3 (159 bytes). Unknown newer versions, malformed
payloads and unreadable existing files disable writes for that snapshot; they
are not replaced with empty counters. Counter additions saturate.

Global statistics remain at `/.crosspoint/global_stats.bin`. Book statistics
are saved under `/.crosspoint/reading_stats/<existing-cache-leaf>/stats_v5.bin`,
outside rebuildable page-cache directories. On first use, the loader looks for
existing statistics in the current cache and then CrossInk's FNV-based EPUB
cache. It reads those sources without moving/deleting them, retains the current
CrossPoint cache identity, and honors existing per-book disable markers.

Bulk and individual cache-clear paths preserve historical statistics and disable
markers, including not-yet-reopened CrossInk EPUB caches. Rebuildable metadata,
sections and covers are invalidated even if statistics cannot be migrated;
original statistics versions and markers remain untouched in that directory.
A failed migration therefore cannot retain stale pages after a book replacement. The normal
finished-book move prepares the matching statistics-directory move and rolls
it back if the book/sidecar move fails; an existing destination history is never
overwritten. Other host-driven renames still need their own migration workflow.

Files are written to a temporary path, synced and closed through the HAL,
read back byte-for-byte, then published with a recoverable backup. Ambiguous
recovery files are preserved and block writes; the reader warns when tracking
cannot start or a save fails. Device-level recovery remains to be tested.
Book and global files are separate transactions; a reset
between them can leave a persistent totals mismatch; automatic reconciliation
is not included. This is not a
cross-file transactional journal.

## Ownership and remaining work

Tracking state belongs to the reader activity, is bounded by a compile-time
size check, and is accessed under the existing render/transition lock. No new
task, framebuffer or per-page heap allocation is introduced. Directory strings
and legacy probes are cold book-open operations. Runtime heap and SD behavior
still require physical measurement.


The saved-total screen includes per-book and all-devices views; see
[reading statistics views](READING_INSIGHTS.md). Editing other statistics and reset,
completion integration, scheduled backups, restore, and complete import/export
remain separate parity work. Nearby-device transfer
is excluded. Existing completed-book counters are displayed;

this group does not automatically mark new books completed.

## Verification

- Focused host cases cover: transaction recovery, old/new formats,
  malformed input, non-destructive import, UTC-free timing, idle/menu exclusion,
  page-turn/end-screen refresh exclusion, timer wraparound, saturation, calendar
  buckets, cache preservation and finished-book moves.
- Individual statistics serial host suite: 928/928 passed (11.34 seconds).
- Individual statistics final X4 Pro/C3 builds and cppcheck passed. Four initial
  reviews and one targeted follow-up completed. The combined candidate's later
  cache-invalidation and end-screen corrections, review results and final build
  sizes are recorded in INTEGRATION.md.
- No physical device check or human design acceptance has been recorded.

Device checks must cover a timed reading session, a long idle, a menu pause,
forward/back/remote/automatic page turns, sleep/reboot persistence, an unset
clock, date rollover, disabled tracking, and cache clearing without loss of
saved statistics. Verify current reading positions before and after testing.

## Review corrections

The initial four reviews identified inline-menu timing, final-page timing,
legacy data on no-session moves, unreadable-file fallback, untranslated duration
formats, unused future editing APIs, and cross-file consistency wording. The
corrections include production overlay-method tests and explicit end-screen,
read-failure and disabled-book move cases. No third review round is planned.
