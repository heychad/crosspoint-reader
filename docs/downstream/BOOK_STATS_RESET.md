# Book statistics reset

`statsbackup::resetBookStatistics` resets one closed book through the existing
backup/restore code. It is not yet connected to the reader menu. An open reader
must flush its statistics and coordinate its session lease before calling it;
the backend refuses calls while a reader lease is active.

The reset clears per-book time, sessions, page counts, pace, dates, and finished
status. It preserves a verified copy of the previous statistics under
`/.crossink-stats-backup/books/<cache identity>/`. Reading position, bookmarks,
preferences, and tracking markers are untouched. A reset of an already-empty
record does not write another backup.

Historical device reading time and page counts remain. Resetting a finished
book adjusts the device's completed-book count through the existing completion
journal. An unfinished book uses the existing recoverable statistics-file
replacement. No storage format, background task, or dependency is added.

The optional expected-current bytes reject a stale confirmation. Missing or
unreadable state, invalid identities, active readers, failed backup writes, and
unsafe file chains prevent the reset. Ambiguous writes retain the backup and
recovery artifacts; some require manual review. The reset does not discard
those artifacts or retry around them.

The implementation shares the existing restore path. Tests cover XTC identity,
preserved reader files, exact backups, completed counts, historical totals,
stale input, active-reader rejection, idempotence, backup collision, write
failure, and preserved recovery artifacts. UI confirmation, live-session
coordination, and device testing remain pending.

Validation: all 1,800 host tests passed, including the 21 backup/reset tests.
The default X4 Pro build passed in 77.03 seconds. This is a backend checkpoint,
not a device-ready reset control.
