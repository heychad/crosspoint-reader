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

Page Overlay now reconstructs the current loaded reader page before reader
teardown when a menu has replaced it. The implementation remains experimental;
image quality and physical display checks remain.

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

E2 remains incomplete. Reader-page reconstruction was added in the next batch
below. Do not install a development checkpoint as a reviewed release candidate.

Checkpoint checks: all 17 targeted tests passed in 4.68 seconds. The final
X4 Pro build with Page Overlay, completion/prompt, statistics restore, and daily
statistics enabled passed in 68.78 seconds. Static RAM is 113,052 bytes and
flash is 5,705,942 bytes for that profile. No firmware was installed.

Chad requested a pause after the current bounded change to preserve weekly
credits. Resume with the reconstruction work above; E2 stays unchecked and
the implementation count stays at 19 of 28 selected major features.

## Reader-page reconstruction

Before replacing the activity stack for sleep, the manager finds the current
reader or the closest retained reader. With Page Overlay selected, the sleep
activity asks that reader to draw its current page into the shared framebuffer
before exit releases the book, per-book settings, fonts, or protected-content
reader. Quick Resume and its timeout override retain precedence. Other sleep
modes do not request reconstruction. There is no second book load or new
network request.

EPUB, TXT, and Markdown reuse the current section's page loader, including
pages already materialized by an unfinished chapter build. The loader restores
the build write cursor. Reconstruction does not extend the chapter, advance
the page, save progress, or record another reading session. It reuses font
prewarming, clipping highlights, reader bars, and the existing image render
cache. Lazy image extraction can still create ordinary image-cache files.

A four-byte signature records the last rendered layout, including font family,
point size, indentation, and the existing render-spec fields. If settings
changed in a still-open dialog, a stale page is not painted using the new font.
A missing section, unavailable page, mismatched layout, or absent framebuffer
falls back to the default sleep screen. No cache or saved progress is deleted
by this fallback. A page that has not yet rendered also takes this fallback.

XTC and XTCH reuse their current reader's bitmap path with a buffer-only target.
That target performs no display refresh, reading observation, or refresh-count
change. Its existing full-page allocation is now checked and released through
RAII: an 800-by-480 bitmap needs 48,000 bytes for XTC or 96,000 for XTCH. Larger
source dimensions require proportionally more; allocation/read failure falls
back. Live XTC rendering keeps its display path and frees its bitmap before
status-bar drawing as before.

Sleep preparation restores the previous orientation, clipping rectangle, and
render mode after drawing. The normal sleep code then materializes night-mode
polarity and composites the selected art. No extra full-screen snapshot,
background task, or persistent state file is added. Reader and popup parents
remain alive during reconstruction; their existing page/font/image allocations
therefore coexist until the manager tears the stack down. Runtime peak heap
and sleep latency remain device measurements.

The reconstructed page is B/W, with dithered images. Transparent overlay art
keeps its existing grayscale path. RC's grayscale page redraw when no art is
available still needs parity work; unavailable chapter layouts do not rebuild
while entering sleep. E2 stays open for those cases and visual acceptance.

Device check: open a book, open its menu, then sleep in Page Overlay mode.
Expect the book under the art, with no menu text. Repeat in all orientations
and night mode, from Text Settings, and with XTC/XTCH. Change font size while
Text Settings is open, then sleep: an unrendered layout should use the default
sleep screen. Wake and verify that the same reading position resumes. Repeat
with Quick Resume after timeout enabled to confirm that it keeps precedence.

November 12 verification: 14 focused checks passed in 10.50 seconds. The full
2,244-case host suite passed in 259.81 seconds. After adding the stale-layout
and partial-build guards, all eight affected sleep checks passed in 5.42
seconds. The default X4 Pro build passed in 78.29 seconds; the final enabled
build passed in 72.67 seconds with 113,052 bytes of static RAM and 5,707,010
bytes of flash. Its ELF sizes are 3,264 bytes for the EPUB reader, 1,168 for the
XTC reader, and 132 for SleepActivity, excluding their owned allocations.
These are build/host results. No new firmware was installed.
