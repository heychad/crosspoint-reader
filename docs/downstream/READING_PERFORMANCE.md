# Reading performance backlog

After the selected first-release features, improve reading speed on the X4 Pro.
Focus on page-turn responsiveness, EPUB rendering, and image-heavy books.

Measure the current device with representative text and image-heavy books, then
identify the slowest steps and test improvements against the same books. Check
page-turn latency, chapter transitions, image rendering, and memory use on the
physical reader. Keep reading positions and protected-book support intact.

## Saving a clipping and turning the page

A successful clipping save now returns to the reader with the selected text
highlighted. It skips the success popup and the redraw when that popup expires.
Saving and exporting still finish before the reader returns. Limits, failed
saves, and failed exports still show a message. On the X4 Pro, save a clip and
try turning the page as soon as the highlight appears.

To check for the slight text shift after a page turn, compare the same text-only
pages with Text Anti-Aliasing on and off. With it on, the reader applies a
grayscale pass that can make the text appear to settle after the first refresh.

## Drawing scaled text

Small caps, superscripts, subscripts, and drop caps now clip each character
before sampling its font bitmap. The renderer calculates the screen rotation
and framebuffer steps once per character and skips bitmap reads when the
character is outside the active area. The change adds no buffers.

An AddressSanitizer and UndefinedBehaviorSanitizer comparison against the
previous renderer matched pixels and character advances in 13,448 cases,
including all four orientations and full and partial grayscale buffers.
Measure page-turn time on the X4 Pro before reporting a speed change.

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

## Finding metadata in EPUB archives

EPUB package lookups read the ZIP directory through the existing 512-byte
buffer. Refills stop at the next sector boundary. A read of a healthy
sector can succeed even if the following sector is unreadable. The buffer
lives for one lookup and falls back to direct reads if allocation fails. Library scans still extract the same title, author, series,
and genre. The reader also uses this lookup when opening EPUBs.

A 300-chapter host EPUB with its package entry near the end of the ZIP
directory took 51 storage reads to locate and stream both its container and
package entries, down from 309. Both implementations returned identical
data. Checks cover names across buffer boundaries, failed reads, and
out-of-range entry lengths. On-device library scan timing is pending.

## Writing decoded EPUB images

The first decode of a JPEG, PNG, or GIF now writes completed rows in bands
from the existing cache buffer. The writer fills off-screen gaps with zeros.
JPEG passes each block's actual output height, so clipped blocks and short
images use the available rows. A short write or failed close discards the
partial cache. The writer uses the existing packed pixel format and stays
within the 24 KiB band limit.

The same 600-row host fixture went from 602 file-write calls to 40, with
byte-identical output. Tests also cover repeated blocks at the same row,
clipped gaps, short writes, and an image shorter than the decoder's block
limit. The first-decode time on an X4 Pro SD card still needs measurement.
This adapts the [CrossInk RC3 image cache change](https://github.com/uxjulia/CrossInk/commit/07346cb24f9ccc19ea46521e6fc6f7a9b17e7b58).
