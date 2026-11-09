# Reading statistics views

The statistics screen offers This device and All devices views. From an open
book's statistics shortcut, it also offers a Book view with reading time,
sessions, progress, pace, saved dates, and time-left estimates when available.
Time left uses current progress and saved reading time. Imported estimates are
preserved in the data file but are not shown because they may be stale.

All devices combines local statistics with compatible files already stored in
`.crosspoint/synced_stats`. It reports incomplete totals if a source cannot be
read. It does not transfer data between readers.

Opening statistics saves a snapshot without ending the current reading session.
While the reader remains active, it retains pending time and retries a failed
save. Retained recovery files can block retries and require review. A failed
save on exit or sleep can still lose pending data. Less than ten
seconds of accumulated reading does not add reading time.
Book and device totals are saved separately, so an interruption between writes
can leave those totals out of sync.

With tracking active for an open book, selecting Started reading or Finished in
its statistics view edits that date. The same controls are available from Library
for a closed book with readable saved statistics. Enter a calendar date in `YYYY-MM-DD`
format (years 2000–2099), or submit an empty field to clear it. Both are manual
overrides; clearing suppresses later automatic start-date assignment. A finish
date does not mark the book finished. Editing from an open book is unavailable if
tracking was inactive when the book opened. Failed saves show an incomplete status;
retained recovery files can block further edits and must be preserved for review.
The open reader retains its edit and retries on exit. Book and device statistics
remain separate writes. Resetting statistics remains separate work. Device validation is
pending: compare the views with saved records and check that reading resumes at
the same position after leaving the screen.

## Charts and history

Each statistics view opens Charts and history. Book, device, and all-devices
views show time-of-day and weekday totals. Device and all-devices views also
show a calendar of recorded reading days, within the stored history window.
The calendar records whether reading occurred, not daily reading duration.
Individual books do not have a calendar because their stored statistics do not
include daily history.
Missing data, an incomplete save, and partial all-devices totals remain visible.
Charts use a snapshot and do not change statistics or reading position. Opening
statistics from a book may first save the current reading session.

On-device checks are pending for navigation, orientations, fonts, calendar
boundaries, memory headroom, and returning to the same reading position.
