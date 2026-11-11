# EPUB menu parity audit

Reference: CrossInk `v2.0.0-rc`, commit `b8feaec4d`, specifically
`EpubReaderMenuModel.h` and `EpubReaderDrawerActivity`. This audit compares the
selected first-release scope, not every option in the upstream font engine.

| RC control or behavior | Fork status |
| --- | --- |
| Five menu tabs; font family/size; spacing; margins; alignment; images; hyphenation; paragraph spacing/indents; embedded style | Built. The fork retains its List and Toolbar modes alongside Tabbed. |
| Chapter, percentage, orientation, footnotes, screenshot, QR, Home, sync progress | Built through existing reader actions. |
| Bookmark toggle/view and clipping save/view | Built. Direct drawer deletion of all bookmarks remains to connect to the existing bookmark controls. |
| Reading statistics and finished/unfinished | Built; completion remains behind its experimental flag. |
| Per-book tracking and statistics deletion | Connected in this batch through the shared reader statistics flow; deletion remains behind the statistics-restore flag. |
| Status bars, controls, render mode, reset book settings | Built. |
| Cache deletion | Confirmed reset now preserves progress, annotations, and settings. Known generated caches rebuild at the saved position; failure offers Retry. See [cache reset](EPUB_CACHE_RESET.md). |
| Reading-pace reset | Built behind the statistics-reset flag. Clears learned seconds per page and the legacy estimate, with an exact backup. Reading history, dates, and completion stay intact. |
| Per-book dictionary selection | Remaining. Check the existing per-book settings contract before adding storage. |
| Indexing-method control | Remaining. Map its behavior against the fork's renderer before adding another setting. |
| Live text preview in typography/layout panes | Remaining. Preserve position and bound preview memory. |
| Stable/publisher page navigation | Later: A8. |
| Automatic page-turn additions | Later: A7; existing fork controls remain available. |
| Focus/guide-dot additions | Later: A6; existing Focus control remains available. |
| Lookup history and independent dictionary font controls | Later: F1 and F3. |
| Publisher-font and full TTF-engine controls | Later: E6. |
| Nearby position/book transfer | Skipped: D5. |

B7 remains partial until the remaining first-release controls, preview,
cumulative review, and device checks are complete. A menu entry alone does not
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
