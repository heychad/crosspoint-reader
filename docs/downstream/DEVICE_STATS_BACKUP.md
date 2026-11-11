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

The current retention selector recognizes only dated `A` snapshots. Manual and
undated snapshots are separate. CrossInk keeps up to seven in each group; that
three-group policy is not implemented here yet. Automatic deletion is not
connected: retention still needs verified file ownership before removing
candidates. Preserve
incomplete files and older `.part` artifacts; interrupted renames can leave
entries sharing storage.

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
