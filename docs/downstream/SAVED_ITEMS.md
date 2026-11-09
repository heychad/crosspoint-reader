# Saved Items

Home pages its menu when space is limited. Buttons follow the selected page.
Vertical swipes move between pages, or between items in the cover-grid layout.

Home → Saved Items lists indexed local books with bookmarks or clippings in
alphabetical title order. Books with both kinds offer a chooser. Opening a kind
loads the normal reader and its existing bookmark or clipping controls. Closing
the list returns to the reader at its saved position. Selecting a saved location
uses the reader's existing progress handling.

In an EPUB, Reader Menu → Saved Items and the toolbar's More → Saved Items
open the same catalog. This leaves the current book through the normal reader
exit, which flushes the session and statistics. The reader saves the current
position during page rendering. Back from the catalog goes to Home; reopen the
book from Recents to continue reading. Saved Items can also be assigned to a
Quick Actions slot or a reader button; edge-swipe bindings do not include it.
Older firmware may discard this new shortcut value, so retain a settings backup
for rollback.

The current bookmark controls support rename and delete. Current clipping
controls support viewing text, deleting, and opening passages with source
anchors. Source-less older clippings remain readable through those controls.

CrossInk EPUB bookmark files in versions 2–5 appear as a separate kind. Their
view preserves the binary files and supports opening saved locations. Paragraph
anchors take priority when available, with chapter fraction as fallback after
pagination. The reader may need to finish indexing that chapter. Both CRC and
legacy hash filenames are read. Entries from both files remain visible, including
duplicates. Editing and consolidating these older files remains migration work.

The catalog follows the existing Library index, supported reflowable extensions,
scan depth and book limit. It skips missing files. A dirty or absent index uses
the existing rebuild path. External moves still require the existing state
migration, since saved content is keyed by book path. Opening the screen performs
a fresh scan. An incomplete scan is reported, and a book with unreadable saved
metadata remains visible for inspection. Older indexes have unknown scan coverage
and show the warning until a Library rebuild detects no coverage gaps. Storage
failures that the underlying directory iterator reports as end-of-directory
cannot be distinguished from a complete walk.
The clipping scanner duplicates the clipping record format and must be updated
when that format changes.

## Resource and lifecycle ownership

The activity owns one checked array with two bytes per indexed book, at most
8 KiB for 4096 books. Titles and authors use reusable 513-byte and 129-byte
buffers. List layout properties and the legacy record scratch belong to the
activity, keeping these large objects off the C3 task stack. The SDK row-provider contract consumes each row before requesting the
next. Metadata scans run in batches of four books on the main loop, under the
existing render lock. They introduce no background task or network connection.

A scan-owned path lookup adds at most 512 bytes and is released after the scan.
It avoids re-reading folder prefixes for every book.

Native bookmarks use the existing streaming JSON parser with text capture
disabled. Its checked allocation exists only while inspecting one file. Clipping
inspection skips validated fields and text without loading the clipping index.
The older bookmark viewer keeps at most two file handles and reads one fixed
record at a time. The catalog checks at most eight legacy records per book; a
larger collection remains available but triggers the incomplete warning because
the remaining entries are checked only when displayed. The catalog array and file handles are released before the
reader starts loading book data. During the transition, the new reader object
can coexist with the outgoing catalog until the manager replaces it.

The reader stores a one-shot request for the selected saved-content screen. It
opens that screen after the first page has rendered. Standard reading events,
recent-book updates and reading statistics therefore apply when a saved book is
opened. The older bookmark position adds transient chapter/paragraph fields to
the in-memory activity result. Existing saved files keep their formats.
The Saved Items bookmark result handler mirrors the reader-menu handler; changes
to bookmark result handling must keep both paths aligned while preserving their
different cancel behavior.

## Verification status

Implementation and host verification are in progress. Independent reviews,
final build coverage, behavior and maintenance acceptance, and physical checks
remain pending.

## Device checks

1. Back up reading state. Open Saved Items with no saved content, native
   bookmarks, clippings, older CrossInk bookmarks, and a mixture in one book.
2. Open, rename and delete a current bookmark. View and delete a clipping, then
   follow an anchored passage. Reopen Saved Items and verify the changes.
3. Open an older bookmark, change font and orientation, then reopen it. Verify
   paragraph or approximate chapter position and compare the original file hash.
4. Exercise Home and list navigation in portrait, inverted and both landscapes,
   using remapped buttons and touch. Check the optional Plugins/OPDS Home slot.
5. Repeatedly enter, cancel and open books. Measure internal and PSRAM free heap,
   largest blocks and task-stack headroom. Cover fresh/partial chapter indexes,
   maximum-size libraries and damaged metadata on a disposable card copy.
