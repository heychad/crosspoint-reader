# EPUB menu parity audit

Reference: CrossInk `v2.0.0-rc`, commit `b8feaec4d`, specifically
`EpubReaderMenuModel.h` and `EpubReaderDrawerActivity`. This audit compares the
selected first-release scope, not every option in the upstream font engine.

| RC control or behavior | Fork status |
| --- | --- |
| Five menu tabs; font family/size; spacing; margins; alignment; images; hyphenation; paragraph spacing/indents; embedded style | Built. The fork retains its List and Toolbar modes alongside Tabbed. |
| Chapter, percentage, orientation, footnotes, screenshot, QR, Home, sync progress | Built through existing reader actions. |
| Bookmark toggle/view and clipping save/view | Built, including confirmed deletion of all native and verified imported bookmarks for the open book. Partial failures offer Retry. |
| Reading statistics and finished/unfinished | Built; completion remains behind its experimental flag. |
| Per-book tracking and statistics deletion | Connected in this batch through the shared reader statistics flow; deletion remains behind the statistics-restore flag. |
| Status bars, controls, render mode, reset book settings | Built. |
| Cache deletion | Confirmed reset now preserves progress, annotations, and settings. Known generated caches rebuild at the saved position; failure offers Retry. See [cache reset](EPUB_CACHE_RESET.md). |
| Reading-pace reset | Built behind the statistics-reset flag. Clears learned seconds per page and the legacy estimate, with an exact backup. Reading history, dates, and completion stay intact. |
| Per-book dictionary selection | Built through the existing book-preference scope. Explicit dictionary/None and Use global remain distinct. See [book settings](BOOK_SETTINGS.md). |
| Indexing-method control | Built. Incremental retains fast first-page display; Full chapter completes uncached/partial chapters before display. Layout and cache formats are unchanged. |
| Live text preview in typography/layout panes | Built using a bounded current-page excerpt and the existing reader text renderer. Font and Layout tabs and their font panes update live; other tabs keep their list space. |
| Stable/publisher page navigation | Later: A8. |
| Automatic page-turn additions | Later: A7; existing fork controls remain available. |
| Focus/guide-dot additions | Later: A6; existing Focus control remains available. |
| Lookup history and independent dictionary font controls | Later: F1 and F3. |
| Publisher-font and full TTF-engine controls | Later: E6. |
| Nearby position/book transfer | Skipped: D5. |

The selected B7 controls and live text preview are implemented. Cumulative
review and device checks are still required. A menu entry alone does not
establish working behavior or visual parity.

Reading-pace reset uses the existing backed-up statistics replacement and retry
flow. The reader flushes pending time before the reset, then reloads the saved
book. Chapter time-left learns from subsequent forward page turns. The current
whole-book estimate still uses total reading time and progress, so this action
does not reset that estimate or erase its underlying history.

Pace-reset checkpoint validation: 48 focused statistics tests passed. The full
host run passed 1,812 of 1,813 checks in 73.65 seconds; its one stale menu-count
fixture was corrected, and all four menu checks then passed in 5.21 seconds.
Default and enabled X4 Pro builds passed in 138.24 and 109.62 seconds. The
menu object measures 4,932 bytes; `ReaderStats` remains 456 bytes. These are
host/build checks; the new action has not been tested on the reader.

Bookmark clearing verifies the imported file headers against the open book,
closes their handles, and removes their recovery siblings before the primary
files. Native JSON bookmarks are removed afterward. Progress, clippings,
statistics, and settings are untouched. Unreadable or recovery-only imported
files block clearing; a failure can leave earlier files cleared, and Retry
continues from the remaining files. This action does not add individual editing
of imported bookmarks. It creates no backup of intentionally deleted bookmarks.

Bookmark-clear checkpoint validation: all 1,818 host tests passed in 62.66
seconds; the four menu checks passed in 4.46 seconds. The default X4 Pro build
passed in 132.50 seconds. A final test-only assertion confirmed that Saved Items
no longer reports the cleared book; its four focused tests passed. The menu
object is 4,992 bytes and the temporary imported-bookmark reader is 52 bytes,
plus its normal file handles and path strings. No new code in this batch is
behind an experimental flag; the existing host menu tests cover their flag
combinations. The exact device-candidate build and hardware check remain.
