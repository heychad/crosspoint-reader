# CrossInk settings migration

The fork translates compatible settings from tagged CrossInk RC2
(`959e3d1575cf`) into CrossPoint 2.0.0 settings. Numeric IDs are not interchangeable.
G3 remains partial; this audit does not declare every old preference supported.

## Library-policy import

The confirmed Library import includes these native policy and layout choices:

| CrossInk field | Native behavior |
| --- | --- |
| `libraryUseMetadata` | Use EPUB metadata or filenames. Library and Saved Items check the index mode on entry and rebuild when needed. |
| `showHiddenFiles` | Show hidden entries in the file browser and web file manager. |
| `removeReadBooksFromRecents` | Apply the configured Recents policy when a book is marked finished. |
| `moveFinishedToReadFolder` | Apply the existing finished-EPUB organization policy. The experimental completion-move path retains its gate and deferred-move journal. |
| `libraryListExpanded` | Select compact or expanded/grouped library rows, using the same 0/1 values as tagged RC2. |

Only integer 0 and 1 are accepted. The confirmed on-device import replaces
supported values; missing-fields-only staging preserves existing values,
including explicit nulls. The current settings are backed up, the CrossInk source
is retained, and unrelated settings remain unchanged. Importing these toggles
does not itself move a book or edit Recents, progress, or reading statistics.

This reuses the existing staging document, storage lock, snapshot, safe-write,
and recovery marker. There is no new task, persistent format, or retained heap
buffer. Imported members share the existing 32 KiB serialized settings limit
and checked overflow path. Runtime heap and physical behavior
remain unmeasured.

## Older preference names

The explicit imports now handle these older CrossInk fields when the modern
source field is absent:

| Older source | Native field | Values |
| --- | --- | --- |
| `readerDarkMode` | `screenInverted` | 0 or 1 |
| `sleepTimeout` | `sleepTimeoutMinutes` | IDs 0–4 map to 1, 5, 10, 15, or 30 minutes |
| `fileBrowserPreview` | `fileBrowserDisplay` | 0/1 select one/two text rows |

A present modern source field takes precedence, including malformed or null
values: the import skips that field rather than using a stale alias. Invalid
alias types and ranges also leave the target unchanged. Missing-only staging
preserves existing native values, including explicit nulls. Confirmed replacement
can update the supported native field. Legacy keys are not added to the native
save; the original CrossInk source stays intact.

These mappings and `libraryListExpanded` reuse the existing settings snapshot,
source reader, staging document, and recoverable writer. The timeout lookup has
five immutable bytes; no new retained buffer or persistent format is introduced.
At that checkpoint, the compiled S3 staging frames were 352 bytes for Preferences
and 192 for Library.
These cold import paths still need runtime stack-headroom and heap-peak
measurement on the device.

The 112 affected checks passed in 2.54 seconds, including exact source/backup
preservation through the production import transaction. Preference and Library
staging passed AddressSanitizer and UndefinedBehaviorSanitizer; preferences ran
under the existing default, backup, and Page Overlay configurations. The default
X4 Pro build passed in 70.12 seconds (113,140 static RAM bytes and 5,673,550 flash
bytes). Logs are `/tmp/x4-migration-alias-checks.log`,
`/tmp/x4-migration-alias-preferences-sanitizers.log`,
`/tmp/x4-migration-alias-library-sanitizers.log`, and
`/tmp/x4-migration-alias-firmware.log`. Independent review and device checks remain.

## Library sort and direction

The native Library now saves its selected tab and each tab's direction in
`libraryViewTab` and `libraryViewDescendingTabs`. A changed tab or direction
uses the existing checked settings writer. Selecting the same tab without
toggling does not write. Failed saves keep the choice in RAM and use the existing
settings warning and retry path. Reopening Library restores the saved view
before filtering or painting. Older settings retain the Recent/newest-first
default.

The explicit Library import translates tagged RC2's `librarySortMethod` and
`librarySortDescending` as one choice:

| RC2 method | Native tab |
| --- | --- |
| 1 Title | Title |
| 2 Author last name | Author, using the canonical display name's surname |
| 3 Author first name | First name, using the folded canonical display name and the Author quick tab |
| 4 Recently opened | The history-only view, using the Recent quick tab and an independent direction |
| 5 Series | Series |
| 6 Genre | Genre |

Both source fields must be valid integers; direction must be 0 or 1. The import
changes only the selected tab's direction bit. Missing-only staging preserves
the whole native pair when either key exists, including null or invalid values.
Confirmed replacement can replace it. The existing transaction backs up the
native settings and preserves the CrossInk source byte for byte.

Date added (0) remains unchanged on import. Tagged RC2 sorts it by file creation
time; the native arrival order uses modification time. A faithful import needs
creation-time capture and freshness checks. Recently opened now maps to logical
view 6 and quick-tab slot 0, with direction bit 6. It reads the resident history
rather than using the combined arrival/history shelf. The default directions
start Recent and Recently opened newest first; existing native masks are retained.
G3 remains partial while Date added and the remaining compatibility boundaries
are resolved.

The first-name view and import are now built, with independent saved direction
and the native scrolling sort picker. Its logical view ID is 5; it uses Author's
quick-tab/navigation slot 2. The two numeric IDs are not interchangeable. The
version 4 index from the preceding checkpoint provides the order; this UI stage
does not change that format. See [index details](LIBRARY_METADATA.md) and
[controls](LIBRARY_BROWSING.md).

The two settings occupy two bytes before enclosing-structure padding. There is
no new task, retained buffer, index version, or reading-state write. The two JSON
members share the existing 32 KiB settings limit and overflow checks. Runtime
save latency and heap/stack headroom still need measurement on the X4 Pro.

For the next reviewed firmware test: select a different Library tab, toggle its
direction, leave and reopen Library, then sleep/wake and reopen it again. Check
that the tab and direction persist and that a previously open book still resumes.

The 205 affected checks passed in 3.92 seconds. The import and extracted
production view-change methods passed AddressSanitizer and
UndefinedBehaviorSanitizer. The default X4 Pro build passed in 106.43 seconds,
with 113,140 static RAM bytes and 5,674,470 flash bytes. The older tab-lock
fixture needed the new settings dependency and a method-bounded extraction;
its checks pass. Logs are `/tmp/x4-library-view-checks-final.log`,
`/tmp/x4-library-view-sanitizers.log`,
`/tmp/x4-library-sort-import-sanitizers.log`, and
`/tmp/x4-library-view-firmware.log`. This is a checked WIP checkpoint;
cumulative release review and physical-device validation remain.

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

`CrossInkLibraryImport` exercises all 15 library toggles with both values,
replacement and missing-only staging, malformed types/ranges, idempotence,
unchanged source, and unrelated settings. The production transaction test checks
published policy/layout values and exact source/backup preservation. The earlier
policy batch passed 136 migration/library checks in 5.59 seconds.
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
