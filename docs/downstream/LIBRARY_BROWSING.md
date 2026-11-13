# Library browsing controls

Settings → Library controls visible book formats, finished books, Library rows,
and File Browser display. Their numeric and boolean keys also work through the
existing web settings API. The explicit CrossInk Library import carries the
compatible choices through the existing backed-up transaction; see
[supported mappings](SETTINGS_MIGRATION.md).

## Sort views

The five quick tabs remain Recent, Title, Author, Series, and Genre. The gear
beside Search opens Sort library, using the existing scrolling selection screen.
It offers Reverse order, Recent and new, Title, Author surname, Series, Genre,
Author first name, Recently opened, and Rebuild library. Holding Confirm while the tabs have focus opens the same picker.

Author first name shares the Author tab's navigation. Its tab label becomes
First name, and expanded headings show the normal name rather than the surname
inversion. Tapping that active tab reverses the first-name order. Surname and
first-name views retain separate direction bits. Choosing another quick tab
then Author selects the surname view; the picker selects first-name order again.
The current view and direction survive reopening Library and a clean restart.

The picker allocates one checked EnumSelectionActivity while retaining the
Library parent. Its compiled X4 Pro object is 1204 bytes; its nine StrId labels
use 18 static bytes. The object and inherited UI resources coexist with the
parent. Runtime heap peak still needs measurement. It adds no book-sized
buffer or new storage writer. A failed allocation leaves Library open. Cancel
and invalid results preserve the current view. The existing input owner and
held-release guard handle returning from the picker.

On the next reviewed firmware, open the gear and choose Author first name.
Check its heading and direction, tap First name to reverse it, then leave and
reopen Library. Also cancel the picker after a Confirm hold and reopen an
in-progress book to check its saved position. Device validation remains pending.

The first-name checkpoint passed 211 affected host checks, including real-font layouts for Author and
First name in both reader orientations. Import, picker/view changes, and
filter/group rendering pass AddressSanitizer and UndefinedBehaviorSanitizer.
The default X4 Pro build passes in 107.65 seconds, with 113140 static RAM bytes
and 5675630 flash bytes. The picker entry frame is 96 bytes. Logs are
`/tmp/x4-first-name-view-checks.log`, `/tmp/x4-first-name-view-sanitizers.log`,
`/tmp/x4-first-name-view-import-sanitizers.log`,
`/tmp/x4-first-name-view-filter-sanitizers.log`, and
`/tmp/x4-first-name-view-firmware.log`. Cumulative handoff review and device
validation remain.

## Recently opened

Recently opened shows only the resident reading history, newest or oldest first.
It shares the Recent quick tab, labelled Opened in this view, and has its own
saved direction. Recent and new keeps the combined arrival/history shelf.
Search uses the displayed title and author; disabling metadata uses the filename
stem and hides the author. Format and finished-book filters still apply.

A missing, stale, or unreadable derived index does not trigger a card scan when
opening history. Available index metadata supplies optional series/genre details;
lookup failures retain the history rows. Covers can load without an index.
Choosing an indexed view opens or rebuilds its index as needed. The existing
book actions, including deletion and explicit rebuilding, retain their normal
storage behavior. Returning from Statistics refreshes the resident mapping.

History now retains 18 books, matching tagged CrossInk RC2. Existing ten-entry
files still load fully; subsequent opens can fill the larger capacity. Rotation
and serialization preserve surviving titles, authors, covers, and order. The
three existing row-mapping arrays occupy 90 bytes in the activity. Optional
identity matching uses batches of eight, with a 128-byte stack array, and keeps
the existing index API's limit.

History search joins at most 512 UTF-8 bytes of title, separator, and author in
existing activity scratch. Folded scratch has the existing four-times expansion
budget. Oversized metadata shows a clear-search instruction; browsing and stored
metadata remain intact. No new task, per-book heap array, reading-state format,
or derived-index version is introduced. Runtime heap peak and latency still
need device measurements.

The full 2355-check host suite passed in 112.04 seconds. History filtering and
rows, picker/preferences, imports, and recent-store loading/rotation passed
AddressSanitizer and UndefinedBehaviorSanitizer. Real-font layouts cover the
Opened and First name labels in both orientations. The X4 Pro build passed in
133.36 seconds, with 113140 static RAM bytes and 5677094 flash bytes.

Compiled X4 Pro sizes are 96 bytes per RecentBook object, excluding string
payloads and vector spare capacity, and 2368 bytes for LibraryListActivity,
excluding owned buffers. The resolver's ABI stack frame is 272 bytes; its
identity array accounts for 128 bytes. These are compiled sizes, not runtime
watermarks or heap-peak measurements. Logs are `/tmp/x4-history-view-full-checks.log`,
`/tmp/x4-history-view-firmware.log`, `/tmp/x4-history-view-size.log`, and the four
`/tmp/x4-history-view-*-sanitizers.log` files. Cumulative handoff review and device
validation remain.

## Filters and rows

Show EPUB, XTC, TXT and Markdown independently. XTC includes `.xtc` and `.xtch`.
Markdown uses the reader's `.md` support. Hide finished books removes entries
whose saved statistics identify them as completed. An unavailable or damaged
statistics record remains visible. Search combines with the selected filters.
Turning a format back on makes matching books visible again.

Recent books use the same visibility rules as indexed books. A visible pin
retains its recent-store identity, and its duplicate in the indexed list is
skipped. Search results use the selected sort order. Reversing the sort and
clearing search rebuild the overlap mapping.

Expanded Library rows use the existing group headings and spacing. Compact
rows use the theme's tight spacing and show each book's author as a subtitle.
The native group-collapse control remains available.

File Browser supports one- or two-line names and an optional trailing file
extension. The firmware picker uses two lines and visible extensions. The
initial defaults show all formats and finished books, use expanded Library
rows and two-line File Browser names. The latter preserves the fork's existing
default.

## State and resources

The browsing preferences use the existing bounded settings serializer.
The current author-ordering work upgrades the derived Library index to version 4; see
[index migration](LIBRARY_METADATA.md). Active search or format/completion filtering uses
the existing array of at most 4096 16-bit row positions, requiring at most
8 KiB. The ordinary unfiltered view allocates no row-position array. Eighteen
recent-store indexes identify the surviving pins. Visible row strings use
the existing page-sized storage.

Completion scans also use a temporary sparse folder lookup with at most 128
32-bit offsets, or 512 bytes. One folder-table pass creates the checkpoints.
Their stride is the folder count divided into at most 128 groups, rounded up.
A 4096-folder index then needs at most 32 folder-header reads per path. The
full 16-bit folder-count range remains supported with a larger stride. The
lookup belongs to one open index generation and is released at scan exit.
Plain search avoids full paths, and format-only filtering reads filenames.

Filtering runs under the owning Library render lock, yields during long
scans, and closes published row routing before rebuilding the model. An
allocation or index-read failure shows a filtering-unavailable message.
Unknown completion data stays visible. Runtime card latency, largest heap
blocks and stack watermarks require device measurements.

Series and genre browsing and the recent-book cover grid are included; see
[series and genre](LIBRARY_METADATA.md) and [recent covers](RECENT_COVERS.md).
Nearby synchronization is excluded. Device verification is pending.
