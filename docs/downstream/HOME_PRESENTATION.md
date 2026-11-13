# Minimal and Dashboard Home

The X4 Pro implementations now include the selected RC2 Home content and controls.
Minimal shows a rounded cover, reading time, and whole-book progress. Dashboard
pairs the cover with reading statistics, then shows a two-line book title and
the saved EPUB chapter title, falling back to the author. In landscape the
statistics use a grid and the title/subtitle each use one line.

| Dashboard data | With a clock | Without a clock |
| --- | --- | --- |
| Reading time, time left, progress, and pace | Shown when available | Shown when available |
| Daily average, days reading, start date, and finished/projected date | Shown when available | Replaced by sessions and average session |
| Device footer | Current streak and dominant reading-time period | Reading time and completed books |

This adapts RC2's combined start-date/day-count row into two rows. The eight
clock-based fields and six clockless fields fit the selected X4 Pro layouts.
Native fonts, SDK popup styling, placeholder artwork, and the existing no-books
prompt remain in use; the deferred font-engine work is not part of E1.

## Data and reading state

Home loads summaries on entry, preview changes, and return from statistics.
Book/global statistics use existing read-only loading. Missing, unreadable,
disabled, or unavailable data stays hidden or shows a dash; it does not become
an invented zero. Time-left and dates use existing native estimates. Pace uses
the native minimum of more than 60 recorded seconds. Global footers require a
present, readable primary statistics snapshot.

The saved chapter title is read through the existing bounded progress helper.
TXT/Markdown progress remains unknown because its validator can change stale
layouts. No settings, progress, statistics, or cache format is rewritten by the
summary loader. Normal thumbnail generation keeps its existing derived-cache
behavior.

## Memory and rendering

The basic 16-byte summary remains separate from the richer, bounded Home record,
so Carousel's three entries do not grow. The detail record is capped at 192
bytes, including 128 bytes for a UTF-8 chapter title. Temporary book/global
objects are allocated sequentially, not retained together.

The existing checked cover snapshot now stores only compact-theme artwork.
Its rectangle drives restoration, cover taps, and thumbnail height. A geometry
change invalidates the snapshot and regenerates the matching thumbnail. At
480×800 the Minimal artwork needs at most roughly 26 KiB after byte alignment;
Dashboard needs at most roughly 17 KiB. These are geometry-based bounds, not
measured runtime heap. The existing default-heap allocator and redraw fallback
remain in use. There is no additional framebuffer, worker, persistent file, or
network connection.

Host drawing checks cover both panel dimensions, both UI sizes, clock and
clockless data, disabled/missing values, every bitmap failure stage, cached art,
and failed snapshots. They check content values, screen bounds, and text overlap
under ASan/UBSan. The read-only loader has separate availability, date, period,
pace, and allocation checks. A host layout preview uses sample covers and mock
font metrics; physical e-ink rendering, peak heap, and latency remain unverified.

See [compact Home controls](COMPACT_HOME_CONTROLS.md) for gestures, Menu,
book statistics, and the short device check. Source comparison:
CrossInk [v2.0.0-rc2](https://github.com/uxjulia/CrossInk/tree/v2.0.0-rc2).

Validation: 2,283/2,284 checks passed in the initial full host run; the single
classic-menu fixture lacked the new layout helper. All five affected checks
then passed in 1.28 seconds. Final strengthened drawing checks passed again.
The first firmware attempt caught an explicit Rect-constructor mismatch that
the original mock permitted; the code and mock now use the native contract.
Both final X4 Pro builds passed: standard in 66.65 seconds, combined experimental
profile in 67.38 seconds. Physical behavior remains unverified.
