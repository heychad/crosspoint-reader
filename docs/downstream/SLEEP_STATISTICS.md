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
