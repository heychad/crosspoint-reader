# Reading performance backlog

After the selected first-release features, improve reading speed on the X4 Pro.
Focus on page-turn responsiveness, EPUB rendering, and image-heavy books.

Measure the current device with representative text and image-heavy books, then
identify the slowest steps and test improvements against the same books. Check
page-turn latency, chapter transitions, image rendering, and memory use on the
physical reader. Keep reading positions and protected-book support intact.

## Saved Items feedback, November 12

- First open took more than 30 seconds. Saved Items now scans the bookmark and
  clipping directories once and skips books with no matching files. It shows the
  loading screen once. A 4,096-book host regression checked only the two books
  with possible saved items. FAT and exFAT tests cover case-insensitive names,
  backup files, and a read failure. If the card changes during the scan, it
  restarts with the original per-book checks. A missing or dirty library index
  still triggers a rebuild. Measure first and repeated opens on the X4 Pro.
- The Saved Items route now opens the saved list as soon as the book loads.
  The first reader frame waits until the user selects a passage or returns
  from the list. Reader initialization starts tracking and records the book in
  Recents. A legacy bookmark can open its exact page when the same chapter has
  a complete cache that matches the current layout and page count. Missing,
  partial, or mismatched caches use the saved passage marker. Host checks cover
  the handoff, exact page, and fallback paths. Device confirmation remains.
- Book → clipping list → clipping actions is the current navigation. Keep the
  grouped model unless its replacement is explicitly scoped.

These are follow-ups in the performance/polish queue after the selected feature
work. The long-press/save crash is fixed in the tested clipfix2 baseline.

## Library rebuild after card changes

Reconciliation reads the previous index's title, source author, series, and
genre together in 512-byte sectors. The builder reuses its filename scratch
after copying the current filename. It accepts rich metadata from index
versions 3–5, and a failed read stops the rebuild before publication.

On a host fixture with 1,474 EPUBs across 211 author folders, the unchanged
rebuild went from 41,273 read calls to 7,554 and from 41,484 seeks to 7,765.
The index bytes matched across both builds. Measure elapsed time on the X4 Pro
after a card change. A fresh index or newly added EPUB still needs metadata
extraction.
