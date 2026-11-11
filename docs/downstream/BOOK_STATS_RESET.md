# Book statistics reset

`statsbackup::resetBookStatistics` resets one closed book through the existing
backup/restore code. Builds with `CROSSPOINT_EXPERIMENTAL_STATS_RESTORE` now
connect it to the EPUB and XTC Delete book statistics menu actions. Confirmation is required.
The reader flushes its statistics, suspends its sole session lease for the reset,
then reloads the saved state before resuming. Other active readers block the reset.

The reset clears per-book time, sessions, page counts, pace, dates, and finished
status. It preserves a verified copy of the previous statistics under
`/.crossink-stats-backup/books/<cache identity>/`. Reading position, bookmarks,
preferences, and tracking markers are untouched. A reset of an already-empty
record does not write another backup. A book with no saved statistics is already
reset, provided its state can be established without read errors.

Historical device reading time and page counts remain. Resetting a finished
book adjusts the device's completed-book count through the existing completion
journal. An unfinished book uses the existing recoverable statistics-file
replacement. No storage format, background task, or dependency is added.

The optional expected-current bytes reject a stale confirmation. Missing or
unreadable state, invalid identities, active readers, failed backup writes, and
unsafe file chains prevent the reset. Ambiguous writes retain the backup and
recovery artifacts; some require manual review. The reset does not discard
those artifacts or retry around them. In the reader, uncertain writes stop live
tracking and offer a return to Home. Ordinary failures offer Retry; a failed
pre-reset flush preserves the live statistics for retry. Cancel does not reset.

The implementation shares the existing restore path. Tests cover XTC identity,
preserved reader files, exact backups, completed counts, historical totals,
stale input, active-reader rejection, idempotence, backup collision, write
failure, and preserved recovery artifacts. Live-session tests check resumed
counters, retained historical totals, disabled tracking, failed flushes, and
stopping old writes after an uncertain reset. Device testing remains pending.

Validation: all 1,806 host tests passed, including live reset, completion,
backup, and recovery checks. After sharing the EPUB/XTC menu flow, X4 Pro builds
passed with reset enabled (76.03 seconds) and in the default configuration
(86.17 seconds). Independent release
review and device validation remain pending.
