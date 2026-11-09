# Manual device statistics backup

Reading statistics > This device > Back up device statistics writes the
currently displayed device totals, time buckets, and history to
`/.crossink-stats-backup/` on the SD card. It does not back up individual book
statistics or reading positions. If the current reading-session snapshot could
not be saved, or the primary device statistics file is missing or unreadable,
the action does not create a backup.

The new file uses the versioned device-statistics format and a content hash in
its filename. Creation writes a new file exclusively and verifies its contents. It never
deliberately overwrites an existing backup.
The numeric sequence is a collision-avoidance key, not a reliable creation
date or chronological order across reboots. The displayed snapshot must still
match the saved primary file when the backup starts.
An interrupted or failed operation may leave an incomplete `.bin` file;
its filename alone does not prove that it is a valid backup. The operation
does not delete that file or intentionally write to the source statistics. A
`.part` file from an older build is preserved. After a failed or interrupted older
backup, its `.part` and `.bin` names can share storage; deleting either can
damage the other. Preserve both until safely copied and checked. If the
candidate name already exists, retry later. The filename format is proposed;
this build does not include restore, reset, automatic backups, or a backup
browser. Checked exFAT lookup can fail on directories with over 4,096 directory
records; each file uses multiple records.
The action shows a saving state but remains synchronous; SD latency and
watchdog behavior are unmeasured.

Before depending on it, test on the X4 Pro with known statistics, verify a
second backup leaves the first intact, check a failed or full SD card, and
confirm reading progress and totals after sleep and reboot. Later restore work
must validate the embedded hash and file format before applying a backup.
