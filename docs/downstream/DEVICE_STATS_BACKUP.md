# Device statistics backups

Reading statistics → This device → Back up device statistics saves the displayed
device totals, time buckets, and history to `/.crossink-stats-backup/`. It does
not back up individual book statistics or reading positions.

The current snapshot must match the saved primary. Missing or unreadable
statistics do not produce an empty replacement backup. Creation uses a new,
exclusive file and verifies the bytes before reporting success. Existing and
incomplete files are preserved. A content hash detects corruption; it does not
prove the SD card has committed the write to durable storage.

## Automatic snapshots

`CROSSPOINT_EXPERIMENTAL_STATS_BACKUP` enables the automatic-backup preference
and sleep-time hook. Tracking and automatic backups must both be enabled.
The hook runs after the reader's sleep transition, with no active statistics
session. It uses the same source and storage-chain checks as dated backups.

| Kind | Filename |
| --- | --- |
| Manual | `stats_cp_<sequence>_M_00000000_000000_<hash>.bin` |
| Dated automatic | `stats_cp_YYYYMMDD_A_<hash>.bin` |
| Undated automatic | `stats_cp_00000000_U_<hash>.bin` |

Without a valid clock, automatic backups use the undated name. Identical
statistics reuse a verified matching snapshot; changed statistics produce a
new name. When the clock becomes valid, the next backup is dated. The manual
sequence prevents collisions but is not a timestamp.

After a verified automatic backup, dated `A` snapshots are pruned to seven.
The current snapshot is always retained; other same-day files sort by hash,
not creation time. Each sleep removes at most eight files and stops starting
new scan work after 1.5 seconds; an individual storage operation can take
longer. A backlog continues on later sleeps. Manual and undated snapshots are
preserved. CrossInk's separate retention for those two groups remains open.

Before deletion, the candidate and current snapshot must pass checksum/format
inspection. Two complete directory scans and an unchanged mutation snapshot
are required. Candidate storage must be disjoint from every sibling file and
existing primary, backup, and temporary device totals. Unreadable files, nested
directories, failed checks, or unavailable memory defer cleanup. Partial files
are preserved. These checks cover known statistics paths, not arbitrary aliases
elsewhere on a damaged card; this is not a filesystem repair tool.

Retention uses a temporary heap context (under 1 KiB), one directory scanner
at a time (under 8 KiB), and up to 512 bytes for pairwise chain checks. The
small decode buffer is released before scanning sibling ownership. Device
latency and memory measurements remain pending.

Restore and reset prototypes remain behind their separate experimental flags.
Their successful operation replaces device totals, not book progress, and
preserves a pre-operation backup. Native FAT/exFAT fault tests cover these
storage paths; hardware validation is still required.

## Device check

On the next reviewed test build, create snapshots with and without a valid
clock. Confirm that they are separate and can be read back, unchanged totals
reuse the same automatic snapshot, and reading position and totals survive
sleep/restart. Confirm that no older backup was removed.

Validation: 31 focused checks passed, including native FAT/exFAT transient and
fail-stop write tests for dated and undated creation. The X4 Pro build with
`CROSSPOINT_EXPERIMENTAL_STATS_BACKUP=1` passed in 193.17 seconds. This is not
a hardware-validation result.

## Retention verification

The November 11 retention change passed 32 focused checks, including real
FAT/exFAT images with transient and fail-stop write errors and read errors.
Across 60 injected failures, tests preserved primary totals and current,
manual, undated, and retained dated snapshots. The retention fixture isolates
filesystem behavior; checksum/format decoding is covered by the separate
device-backup tests. The X4 Pro build with automatic backups enabled passed
in 71.99 seconds. These host results do not establish physical SD power-loss
behavior or device latency.
