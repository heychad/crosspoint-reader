# CrossInk settings migration

The fork translates compatible settings from tagged CrossInk RC2
(`959e3d1575cf`) into CrossPoint 2.0.0 settings. Numeric IDs are not interchangeable.
G3 remains partial; this audit does not declare every old preference supported.

## Library-policy import

The confirmed Library import now includes four existing native toggles:

| CrossInk field | Native behavior |
| --- | --- |
| `libraryUseMetadata` | Use EPUB metadata or filenames. Library and Saved Items check the index mode on entry and rebuild when needed. |
| `showHiddenFiles` | Show hidden entries in the file browser and web file manager. |
| `removeReadBooksFromRecents` | Apply the configured Recents policy when a book is marked finished. |
| `moveFinishedToReadFolder` | Apply the existing finished-EPUB organization policy. The experimental completion-move path retains its gate and deferred-move journal. |

Only integer 0 and 1 are accepted. The confirmed on-device import replaces
supported values; missing-fields-only staging preserves existing values,
including explicit nulls. The current settings are backed up, the CrossInk source
is retained, and unrelated settings remain unchanged. Importing these toggles
does not itself move a book or edit Recents, progress, or reading statistics.

This reuses the existing staging document, storage lock, snapshot, safe-write,
and recovery marker. There is no new task, persistent format, or retained heap
buffer. Up to four additional JSON members share the existing 32 KiB serialized
settings limit and checked overflow path. Runtime heap and physical behavior
remain unmeasured.

## Open compatibility boundaries

| Source behavior | Current handling |
| --- | --- |
| Built-in font family and size | CrossInk has Lexend Deca and Bitter; this CrossPoint base has built-in Noto Sans. Their IDs and legacy size slots are not equivalent. Built-in selections are left unchanged. Font pack and full font-engine parity are Later. |
| Named SD font and point size | Compatible names and paired point sizes import through global preferences and owned per-book fields. Installing or converting fonts is separate. |
| RC character spacing | Exact whole-pixel values import; half-pixel values remain unsupported. |
| Word and line spacing | Default word spacing and exact native line-height presets import. Other values remain unchanged. |
| Focus/guide, publisher fonts/pages, and stable pagination | Preserve their source data. Deferred features need their own migration checks when enabled. |
| Status bars | The explicit bars import translates supported custom slots and legacy flat fields. Unsupported stable-page or disabled chapter-time requests leave the current bars unchanged. RC2 centering, separators, hidden-bar state, per-bar battery styles, and chapter percentage now import through the [native bar controls](RC2_READER_BARS.md). |
| Device, date, frontlight schedule, and dictionary typography | Their additional CrossInk controls are Later; do not copy raw IDs into different native fields. |
| Clock synchronization, credentials, and plugin state | These are not restored by the library/preferences import. Retain the current CrossPoint configuration. |

The controls, bar, global-preference, and per-book import paths retain separate
checks and confirmations. See [import behavior](SHORTCUT_MIGRATION_PREVIEW.md)
and [feature scope](FEATURE_PARITY.md) for their supported values and gates.

## Legacy flat reader bars

When `readerStatusBars` is absent, the confirmed bars import now translates
flat chapter-page count, book percentage and precision, title, time-left,
battery, progress mode/thickness, and clock choices. The six older coarse
presets supply defaults; explicit flat fields override them. Missing fields
use CrossInk's defaults. A source with no related fields reports Missing.

Placement follows tagged CrossInk: clock centered above the reader; battery
and time-left on the left, title in the center, and page count/percentage on
the right below the reader. The existing native bar writer stores the result.
There is no reading-state or cache-format change and no new buffer or task.
The decoder uses fixed local values and a 30-byte immutable preset table;
existing transaction allocations and overflow handling remain in use.

A present custom layout takes priority. A damaged or unknown custom version
is rejected instead of falling back to stale flat fields. Invalid flat types
and ranges also leave the current bars unchanged. Stable-page requests are
unsupported; chapter time requires its experimental build flag. Both failures
occur before snapshot or settings writes. The later RC2 bar stage also adds the styling controls listed above.

## Verification

`CrossInkLibraryImport` exercises all 14 library toggles with both values,
replacement and missing-only staging, malformed types/ranges, idempotence,
unchanged source, and unrelated settings. The production transaction test checks
the four new fields in the published settings and verifies exact source/backup
preservation. The focused migration/library run passed 136 checks in 5.59 seconds.
The X4 Pro build passed in 66.81 seconds: static RAM 113,132 bytes and flash
5,663,518 bytes. The next cumulative review/device test remains pending.

The flat-bar matrix checks 5,832 combinations in each of the default and
chapter-time configurations, with sanitizer coverage, plus presets, malformed
values, source preservation, idempotence, and custom-layout priority. The
production transaction test verifies published slots and an unchanged source
and exact settings snapshot. All 148 focused checks passed in 10.88 seconds
after correcting a missing include in the new transaction test. The standard
X4 Pro build passed in 71.23 seconds (113,132 static RAM bytes; 5,664,386 flash
bytes). The combined profile also passed in 164.05 seconds (113,060 static RAM bytes;
5,746,750 flash bytes). It enables completion/prompt/move, statistics restore,
daily statistics, Page Overlay, Carousel, and chapter time—not every experimental
option. The standard ELF shows a 112-byte decoder frame; total call-stack
headroom still needs device measurement. Cumulative review and device acceptance
remain pending.

For the device check, back up the card, import a disposable source, and verify
the four choices after restart. Change metadata mode once and allow the index
to rebuild. Confirm that no book moved and that reading position, Recents, and
source/backup files stayed intact during import. Test completion behavior
separately under the intended completion flags.
