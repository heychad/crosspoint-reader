# Reading statistics sleep screen

Settings > Display > Sleep Screen > Reading statistics shows saved data for the
last opened book. The dashboard includes progress, time, sessions, estimated
time left, and page-turn rate. When the device clock is valid, it also shows
dates and the book's time-of-day and weekday charts. Without a valid clock it
shows saved device totals instead. Missing progress, dates, and device totals
show a dash; dates use `YYYY-MM-DD`. Other-device aggregates are not included
in this sleep screen. TXT and Markdown progress is left unknown because their
cache validation can delete stale caches; EPUB and XTC progress are read.

The screen falls back to the normal default sleep screen when reading tracking
is off for the device or book, the book or its saved statistics cannot be read,
allocation fails, or the layout does not fit. Quick resume keeps its existing
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
