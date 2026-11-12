# Reading performance backlog

After the selected first-release features, improve reading speed on the X4 Pro.
Focus on page-turn responsiveness, EPUB rendering, and image-heavy books.

Measure the current device with representative text and image-heavy books, then
identify the slowest steps and test improvements against the same books. Check
page-turn latency, chapter transitions, image rendering, and memory use on the
physical reader. Keep reading positions and protected-book support intact.

## Saved Items feedback, November 12

- First open took more than 30 seconds; Chad clarified that this was only the
  first open. Measure first and repeated opens separately before changing caches.
- Opening a book's saved content briefly paints the reading page before showing
  its clipping list. The current `pendingSavedView` route waits for `pageRendered`.
  Remove that visual transition without changing reading position or tracking.
- Book → clipping list → clipping actions is the current navigation. Keep the
  grouped model unless its replacement is explicitly scoped.

These are follow-ups in the performance/polish queue after the selected feature
work. The long-press/save crash is fixed in the tested clipfix2 baseline.
