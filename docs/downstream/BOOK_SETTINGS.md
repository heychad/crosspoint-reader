# Per-book reader settings

This stage adds CrossInk-style per-book overrides for the reflowable reader in the personal CrossPoint fork. EPUB, TXT, and Markdown share this reader. Global Settings still sets the defaults for other books.

Changing the font family, font size, line spacing, word spacing, character spacing, margin, alignment, paragraph spacing or indentation, hyphenation, embedded styles, antialiasing, image handling, EPUB rendering mode, orientation, focus reading, or chapter indexing while a book is open creates an override for that book. Reset book to global defaults is available in the list menu and More panel. Reset retains the current content position while rebuilding the layout.

Only fields that the reader changes become overrides. Existing overrides stay pinned if a later global default happens to equal their value. Setting an override back to the current global default removes it. Dictionary choice has explicit ownership: selecting a dictionary or None pins
that choice even if it matches today's global value. Use global dictionary
removes only that override. Reset book to global defaults clears it along with
the layout overrides. Hardware controls, status bars, networking, night mode,
and other device preferences remain global.

If an SD-card font is missing or fails to load while a book is open, the reader
uses its built-in fallback without replacing the book's saved font or size. It
can load the selected font again when that font becomes available.

## Storage and ownership

`BookReaderSettingsScope` holds the global layout/dictionary defaults and the last effective book settings as two fixed-size snapshots. `CrossPointSettings` owns this scope and serializes its transitions with the existing store mutex. Global JSON saves project the saved defaults while the reader is open. Global JSON reloads update inherited defaults and reapply only the book's overrides. Normal reader rendering continues to read the effective settings without taking that mutex.

`BookReaderPreferences` owns two paths and the write-permission flag for the reader lifetime. It loads before fonts, orientation, and EPUB parsing. It saves at layout changes, settings-menu return, sleep, and exit. The existing font pickers may still save global settings, whose serializer excludes active book overrides. Exit restores the global layout after the reader session is flushed.

Files use `/.crosspoint/reading_stats/epub_<existing-book-hash>/reader_prefs.json`. This existing durable directory already participates in the finished-book move transaction. The format is version 1 with a sparse `overrides` object. Known keys use CrossPoint's global JSON spelling. Unknown top-level and override keys survive saves. Reset clears supported overrides and preserves unknown keys.

Reading accepts at most 4096 bytes. Invalid, oversized, unreadable, or unsupported versions disable writes for the open book. A recovered temporary file is promoted before its path is reused. A save writes and verifies a temporary file, renames the previous file to a backup, then promotes the temporary file. A failed promotion attempts rollback. On reopen, an existing primary file takes precedence, followed by the backup and then the temporary file. Primary corruption is preserved for review. This protects recoverability without claiming that a FAT rename survives every power-loss scenario.

Cache clearing preserves both the native preference file and CrossInk `reader_settings.bin` files, including their transaction artifacts. A preference file found under a cache or a legacy FNV cache identity is migrated on save. Finished-book moves first preserve a recovery copy in the old cache. An unreadable preference file blocks that automatic move and keeps the book in place.

When no native preferences exist, the reader can load compatible values from a
CrossInk `reader_settings.bin` in the current or legacy FNV cache. Existing
native preferences take precedence. An unchanged import remains read-only; an
edit or reset saves native preferences, while preserving the original binary.
Safe Mode, unreadable records, and recovery artifacts block writes. Font size
also stays in the original record until its font family can be migrated safely.
Finished-book
moves are blocked while a legacy settings file remains at the old path. Font
families and other unsupported values are not imported; full migration remains
pending and this behavior still requires device validation.

`scripts/preview_crossink_book_settings.py` inspects a local copy of a CrossInk
record without writing settings. It reports directly comparable values as
candidates and lists remaining settings for review. Versions 1–13 are decoded;
an optional local `--current` native preferences copy identifies conflicts.
An existing native file takes precedence as a whole; the tool does not propose
a patch or apply a migration.

## Resource budget

Each preference snapshot is 80 bytes; the scope is 168 bytes, including its two snapshots, masks, and flag. The reader store adds two path strings and a Boolean. No task, framebuffer, or per-page allocation is added. JSON parsing and serialization allocate only during cold settings/file operations. Input and output use checked fixed-size buffers of at most 4097 bytes. JSON nodes coexist with the input buffer during parsing and with the output buffer during save. Exact allocator overhead and runtime stack/heap headroom still require device measurement. The byte verification uses a 64-byte stack buffer. The menu adds one row to the existing reserved list.

## Hardware checks

1. Open an EPUB at a known passage. Change font size, orientation, and one spacing option through both available reader menus. Exit and reopen. The book should retain those settings and its passage.
2. Open a second book. It should use the global defaults. Open global Settings and change a default. A book without that override should inherit the new default. A book with that override should retain its value.
3. Reset the first book to global defaults. It should retain its passage, rebuild its layout, and inherit the current defaults after another reopen.
4. Repeat with an SD font, missing SD font, TXT, and Markdown. Check portrait, inverted portrait, and both landscape orientations.
5. Open a font picker, change an option, then sleep before closing the picker. Wake and reopen the book. Check that the book preference survived and global settings stayed separate.
6. Clear the book's page cache. Its preferences and reading state should survive. Complete a disposable book with move-to-Read enabled and verify its preferences after the move.
7. With a backup in hand, test a write failure or unsupported preference version. Saving should report failure after the next reader repaint, keep the existing file intact, and leave global defaults intact after exit. Sleep and exit log failures when a dialog cannot be retained.

Software checks, review coverage, and firmware hashes are recorded with the candidate artifact. Hardware validation is pending.

## CrossInk RC records

Versions 11–13 add image grayscale, half-pixel character spacing, and publisher
fonts. The decoder validates the version-specific byte count and override mask.
The preview retains unsupported fields rather than treating them as older data.

Image Display plus an explicit grayscale override maps to the matching native
Display or Display (B&W) choice only when both fields are owned by the book.
Whole-pixel character spacing within the native -2 to +2 range maps exactly;
half-pixel values, wider spacing, fonts, and publisher-font choices remain
unmapped. The original CrossInk file is never rewritten. Existing native book
preferences still take precedence, and CrossInk Safe Mode still prevents a
partial import.

An explicit Default, Balanced, or Light rendering-profile flag now imports
independently of the layout mask. The snapshot's incidental profile does not
create an override. Invalid profile values remain unmapped. Reset to global
settings records the reset in native preferences and leaves the CrossInk
source intact. Safe Mode is still excluded: it temporarily changes style,
focus, and guide settings and retains their underlying values for later use.

## Per-book dictionary

The reader menu and toolbar More open a scrolling dictionary picker. It owns
folder names discovered by the existing registry, plus Use global dictionary
and None. No dictionary data remains open in this picker. Folder names must fit
the existing 31-byte setting; longer names are not selectable.

The existing version-1 JSON stores `dictionaryName` in `overrides`. A string
pins that folder, an empty string pins None, and null records inheritance.
An absent key permits import from CrossInk's `dictionary.bin` in the current
or legacy FNV book cache. Null prevents the old file from reapplying after a
user selects Use global. Layout settings do not need to be discarded to import
an older dictionary choice.

Legacy paths under `/dictionaries` and `/.dictionaries` map from folder/index
stem to the native folder name. Files are read at most 127 bytes and never
rewritten. Unsupported paths, read errors, and a same-named folder in both
roots prevent automatic import and keep the source intact. The native registry
still determines the actual index file. Global legacy dictionary import is
separate from this per-book change.

For the next device check, give two books different dictionaries, reopen both,
then select None and Use global in turn. Confirm that lookup follows each book
and that exiting restores the global choice. Change a global default while a
book has a pinned choice; that book should retain its choice.

The dictionary picker object is 1,228 bytes before its discovered entries and
path strings; each `DictionaryEntry` is 48 bytes before string storage. It uses
the existing registry's directory scan and holds that list only while open.
Rows borrow those owned names, so repainting does not allocate a new list.
The EPUB menu object is 5,204 bytes. Device heap headroom still needs measurement.

Dictionary checkpoint validation: 75 focused settings tests, the picker and
reader handoff test, and the four menu preflight checks passed. After correcting
two source-extraction boundaries, the final full suite passed all 1,827 checks
in 42.07 seconds. The final incremental X4 Pro build passed in 37.16 seconds
(the initial build took 121.66 seconds). Hardware behavior remains unverified.

Rendering-profile import checks: 95 affected host tests passed in 0.91 seconds.
The firmware decoder matched 2,112 reference and malformed-record cases.
The default X4 Pro firmware built in 67.14 seconds.
No additional settings field, allocation, or persistent format was introduced.
The existing native override and reset paths handle the imported profile.
Device verification remains pending: open a book with a legacy Balanced or Light
profile, confirm the selected profile, reset it to global, and check the passage.

## Independent margin port

The pinned CrossInk RC has separate horizontal and vertical margins, including
values outside this fork's old 5–40-pixel common-margin control. The viewport
primitive now accepts both axes and reserves top/bottom bars against the
vertical margin. The existing one-margin signature delegates with equal axes,
so current callers retain their layout. It adds no stored state or allocation.

A5 remains partial until the settings, per-book ownership, menu/preview, and
legacy import use the new axes. Do not mark that work complete based on the
helper alone. Tests cover portrait/landscape dimensions, oriented insets,
unequal margins up to 150 pixels, bar reservations, and single-margin
compatibility. The focused layout/shortcut/menu run passed 29 checks in 0.60 s.

The default X4 Pro build passed after this viewport-core change in 110.18
seconds. The new axis overload is not yet called with separate saved values;
this checkpoint does not expose new margin controls on the device.
