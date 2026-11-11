# Reading statistics sleep screen

Settings > Display > Sleep Screen > Reading statistics shows saved data for the
last opened book. The dashboard includes progress, time, sessions, estimated
time left, and page-turn rate. When the device clock is valid, it also shows
dates and the book's time-of-day and weekday charts. Without a valid clock it
shows saved device totals instead. When other-device files are available, an
additional All devices card shows the local aggregate. If aggregation reports
skipped or unreadable sources, that card says Partial totals. Missing progress,
dates, and device totals show a dash; dates use `YYYY-MM-DD`. TXT and Markdown progress is left unknown because their
cache validation can delete stale caches; EPUB and XTC progress are read.

If tracking is off or book statistics are unavailable, Reading statistics
uses the Minimal book screen. Missing books, allocation failures, or layouts
that do not fit can still fall back to the default sleep screen. Quick resume keeps its existing
precedence. The sleep cover filter controls inversion. The screen reads existing
data and does not promote interrupted statistics saves or change the book
position. Device totals require a readable primary file. As with the existing statistics
screen, an interrupted separate book/device save can leave totals mismatched.

The new stored mode follows the existing CrossPoint values; older firmware may
fall back or reset this choice. Preserve the settings backup for rollback.
X4 Pro testing must cover a book with and without statistics, tracking off,
missing files, valid and unset clocks, both filter polarities, long and
non-Latin titles, sleep and wake, and the saved reading position. Check sleep
latency and available memory on a representative SD card.

The no-clock aggregate uses the existing read-only statistics loader and adds
one `GlobalStatsAggregate` to the temporary sleep-data allocation. Host size
inspection reports 436 bytes for the combined data, including the 176-byte
aggregate; device peak heap is not yet measured. The data is released after
the render call. It adds no writer, network request, or
background task. Layout checks cover portrait, landscape, short displays, and
partial totals; physical display and latency checks remain pending.

## X4 Pro comparison with the tagged RC

CrossInk `v2.0.0-rc` (`b8feaec4d`) offers Minimal and Minimal Stats separately,
but its `MinimalTheme::drawStatsOverlay` returns immediately on devices other
than X3. Mapping both to this fork's Minimal book screen therefore preserves
the X4 Pro behavior; an additional identical mode is unnecessary.

The remaining Page Overlay work is separate: restore the reader page when the
retained framebuffer no longer contains it, then validate the image-selection
and state-save recovery paths. The existing prototype remains experimental.

## Page Overlay state-save reliability

With `CROSSPOINT_EXPERIMENTAL_PAGE_OVERLAY` enabled, ordinary application-state
saves now use the same checked writer as favorite/folder edits. The old direct
writer is no longer used by this configuration. It validates an existing
primary, stages and verifies the new JSON, then promotes it while preserving
the previous state. It refuses unresolved sleep/book-move artifacts, unreadable
state, or uncertain I/O. The default configuration is unchanged.

Home's Retry now attempts to save the current in-memory state after artifact
recovery. It also works when a failure left no recovery files. An unresolved
artifact or unreadable primary still blocks the save and keeps the warning.
This does not automatically repair a corrupt primary or choose between
conflicting book-move and sleep backups.

The existing writer retains numbered history files and fails after its 10,000
archive slots are occupied; this change does not delete that history. Saves
serialize at most 65,536 JSON bytes into a checked buffer of at most 65,537
bytes, alongside the existing JSON document. Previous-state validation uses
the existing bounded reader and releases its document before serialization.
No task, network activity, persistent RAM cache, or new storage format is added.
This protects staged writes under the existing filesystem contract; physical
power-loss testing and cumulative release review remain pending.

The host checks cover the ordinary-save call path, corrupt-primary refusal,
retry without artifacts, artifact fencing, every short-write boundary of a
sample state, failed sync/close, and recovery followed by another save. The
normal writer and recovery tests use storage doubles, not physical SD faults.

E2 remains incomplete. Resume with reader-page reconstruction when a menu has
replaced the retained framebuffer, then complete the remaining recovery and
visual checks. Do not install this checkpoint as a reviewed release candidate.

Checkpoint checks: all 17 targeted tests passed in 4.68 seconds. The final
X4 Pro build with Page Overlay, completion/prompt, statistics restore, and daily
statistics enabled passed in 68.78 seconds. Static RAM is 113,052 bytes and
flash is 5,705,942 bytes for that profile. No firmware was installed.

Chad requested a pause after the current bounded change to preserve weekly
credits. Resume with the reconstruction work above; E2 stays unchecked and
the implementation count stays at 19 of 28 selected major features.
