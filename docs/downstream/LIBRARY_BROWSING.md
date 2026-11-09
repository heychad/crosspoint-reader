# Library browsing controls

The chad.34 candidate adds settings for visible book formats, finished books,
Library row layout and File Browser display. Settings → Library contains the
controls. Their numeric and boolean keys also work through the existing web
settings API. CrossInk settings import is not included in this candidate.

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

The eight browsing preferences use the existing bounded settings serializer.
Series and genre browsing upgrades the Library index to version 3; see
[index migration](LIBRARY_METADATA.md). Active search or format/completion filtering uses
the existing array of at most 4096 16-bit row positions, requiring at most
8 KiB. The ordinary unfiltered view allocates no row-position array. Ten
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
