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
| Undated automatic, numbered | `stats_cp_<sequence>_U_00000000_000000_<hash>.bin` |
| Legacy undated automatic, readable | `stats_cp_00000000_U_<hash>.bin` |

Without a valid clock, automatic backups use the undated name. Identical
statistics reuse a verified matching snapshot; changed statistics produce a
new name. When the clock becomes valid, the next backup is dated. With experimental backups enabled, manual and undated snapshots use a sequence
one greater than the largest stored number in their own group. A complete,
unchanged directory scan is required. This survives reboot and avoids relying
on uptime as a timestamp; overflow or scan failure stops creation. The default
manual-only configuration retains its existing uptime naming behavior.
Legacy undated snapshots remain readable and are not automatically deleted.

After a verified backup, its recognized group is pruned to seven: dated `A`,
manual `M`, or numbered undated `U`. Groups are independent. The current
snapshot is always retained; dated files sort by date and then hash, and
numbered files sort by their sequence and then hash. Each sleep removes at most eight files and stops starting
new scan work after 1.5 seconds; an individual storage operation can take
longer. A backlog continues on later sleeps. Other groups and legacy hash-only undated snapshots are preserved.

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

## Group retention

The numbered manual and clockless policy follows CrossInk development
`1990a78f2`'s independent group retention and on-card maximum-plus-one naming.
The fork keeps checksummed filenames and exclusive writes. Existing manual
numbers may represent uptime, so their numeric order is not proof of historical
creation order. New manual numbers continue above that existing maximum.

All 34 existing focused checks and four new enabled-feature tests passed
after extending retention to these groups, including checked decoding of the
new undated name and the writer's deduplication and failure paths. The FAT/exFAT fixture also
checks independent manual/undated pruning and deriving the next sequence.
The X4 Pro build with backups enabled passed in 69.98 seconds. Release review
and device validation are still required.
