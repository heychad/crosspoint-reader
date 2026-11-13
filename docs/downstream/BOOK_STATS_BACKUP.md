# Manual book statistics backup

From an open book, Reading statistics > Book > Back up book statistics copies
the saved `stats_v5.bin` into
`/.crossink-stats-backup/books/<cache-identity>/`. It does not back up reading
position, the EPUB, or other books. It runs only if the displayed snapshot is
saved and matches the primary file byte for byte. A missing, legacy-only,
unreadable, or changed primary is reported as a failure; no empty record is
created. Open and read the book with tracking enabled to produce a current
primary before backing it up.

Each backup uses the current book-cache identity, a sequence, and a content
hash in its name. The sequence is not a date. Book paths can change the cache
identity, so the folder name alone is not a permanent book identifier. The
backup writes a new file exclusively, checks sync and close, and verifies its contents; it never
deliberately replaces a prior backup. An interrupted or failed write can leave
an incomplete `.bin` file, so its filename alone does not prove validity. A
`.part` file from an older build is preserved. After a failed or interrupted older
backup, its `.part` and `.bin` names can share storage; deleting either can
damage the other. Preserve both until safely copied and checked.
The content hash is for identification, not cryptographic authentication.
With `CROSSPOINT_EXPERIMENTAL_STATS_RESTORE`, a closed book’s Statistics
screen offers Restore book statistics through the backup picker and a
confirmation. An open reader must be closed first. Restore/reset require a
verified pre-operation backup; failed sync returns BackupFailed before changing
the saved statistics. Failed/incomplete backup files are preserved. Keep a
separate verified backup of reading positions and settings before changing firmware.

The write runs synchronously on the reader's main task; SD latency and watchdog
behavior remain unmeasured. On-device checks should create two backups, verify
their bytes and the book's progress, and exercise a full or unreadable card.
