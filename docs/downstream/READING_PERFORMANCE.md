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
  from the list. Reader initialization still starts tracking and records the
  book in Recents. Host checks cover the handoff and fallback paths. Device
  confirmation remains.
- Book → clipping list → clipping actions is the current navigation. Keep the
  grouped model unless its replacement is explicitly scoped.

These are follow-ups in the performance/polish queue after the selected feature
work. The long-press/save crash is fixed in the tested clipfix2 baseline.
