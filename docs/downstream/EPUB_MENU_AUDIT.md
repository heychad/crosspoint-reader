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
| Cache deletion | Existing EPUB action. Its confirmation and reader-state preservation still need comparison with the RC flow. |
| Reading-pace reset | Remaining. Reuse statistics persistence and preserve unrelated counters. |
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
