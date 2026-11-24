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

### Why text can appear to settle

The CrossPoint 2.0.0 SDK sent two panel refresh commands for an ordinary
antialiased text turn on all three X4 Pro controllers in a host bus trace.
The SDK pinned by CrossInk RC3 sent one on repeated turns with the SSD1677,
UC8179, and supported UC8279 variants. The SSD1677 still sent two on the
first transition in this fixture. Both SDKs ran the same 800 × 480 test page
twice. In a separate mixed-content trace the RC3 renderer still sent two
commands for four-tone images. An ordinary text turn also used two when
PSRAM allocation failed. These counts measure controller commands, not time
or visible ghosting.

RC3's text path reserves 576,000 bytes of PSRAM for this panel geometry.
It uses page-type hints and charge history that must stay consistent when
switching between a book, menus, illustrations, and sleep. Test71 and test72
use the stable SDK. For a physical comparison, **Settings → System → About**
lists **Display Controller**. Compare the same text page with text antialiasing
on and off; the reader may still do its scheduled cleanup refresh.

Sources: [stable display SDK](https://github.com/Free-Ink/freeink-sdk/tree/5e94fe7480f019c89fbf691aef87523eb1b46704/libs/display/FreeInkDisplay),
[RC3 text helper](https://github.com/Free-Ink/freeink-sdk/blob/9d5bba3ef02a1c35363ecadbb50ad0e1ac279653/libs/display/FreeInkDisplay/src/driver/X4ProTextAa.h),
and [RC3 page scopes](https://github.com/uxjulia/CrossInk/blob/0b939f41ec22c0d647a5c65ec5126141cbe7e565/src/activities/reader/ReaderRefreshScope.h).

## Remapped EPUB page button

The physical Up button can queue a remapped next or previous EPUB page on
press. This applies when its effective hold action, the Up + Down chord, and
the Power + Up chord are disabled, Down has no input in the same frame, and a
previous page turn is not pending.
The reader consumes the rest of the Up contact, including release, so an
end-of-book menu cannot reuse it. A remapped Down tap waits for release to
support Power + Down screenshots. This adapts the
[CrossInk RC3 press handling](https://github.com/uxjulia/CrossInk/commit/69e8c40a94c048b0570bc9f9da40742153fc0168).

On the X4 Pro, map Up to Next page and disable its hold and chords. Hold Up on
the final page. Confirm the turn starts while the button is held and its
release leaves the end-of-book selection in place. Then restore a chord and
check that the same press can start the chord before a turn is queued.

## Drawing scaled text

Small caps, superscripts, subscripts, and drop caps now clip each character
before sampling its font bitmap. The renderer calculates the screen rotation
and framebuffer steps once per character. A character outside the active
area skips the bitmap fetch after its metrics are known. A cold SD font can
still read bitmap data during the metrics lookup. The change adds no buffers.

An AddressSanitizer and UndefinedBehaviorSanitizer comparison against the
previous renderer matched pixels and character advances in 13,448 cases,
including all four orientations and full and partial grayscale buffers.
Measure page-turn time on the X4 Pro before reporting a speed change.

## Choosing which font styles to cache

When an EPUB page uses more compressed font styles than the eight available
slots, the reader tries to prewarm the most-used styles first. Repeated glyphs
count toward that choice, while each style still stores each glyph only once.
Less-used styles can still draw through the existing on-demand cache.
The ordering preserves SD and vector font positions. It uses 32 bytes of
counters and sorts the existing packed glyph buffer without heap allocation.
This adapts the [CrossInk RC3 font prewarm change](https://github.com/uxjulia/CrossInk/commit/4eaf212e77def4bc1a6e9c322ee3f556b09800cb).

A host fixture with twelve compressed styles and an SD font checks the
eight admitted slots, equal-use ordering, mixed-font order, and multibyte
glyphs. In that fixture, the admitted styles cover 21 of 25 glyph uses,
up from 11 with the previous ordering. Measure render time and memory
headroom on a mixed-style page on the X4 Pro.

### When next-page fonts load

The EPUB reader handles a waiting page turn or menu action before starting
next-page font preparation. It waits for a quiet input frame and more than
400 ms since the last render. A button or touch still held, or a queued turn,
also defers the background indexing tick and next-chapter prefetch. A press
that arrives after a synchronous read has started can still wait for it.
When reading approaches the last cached page in a partly indexed chapter,
the reader also waits for a quiet frame before starting optional indexing.
A turn past that page extends the current chapter on demand. The first
uncached page can still take longer while the reader lays it out.
The font scan collects glyphs in visual order and skips baseline measurements
until the text is drawn.

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

Saved Items reserves a 512-byte string for scanning book paths. The follow-up
reads each folder and filename directly into that string. On the backed-up
catalog of 1,935 books in 161 folders, this removed 5,549 path-building heap
allocations in a host scan. All paths and their order matched byte for byte.
Both scans made 8,637 index reads, excluding file open and lookup setup.

Test76 reads four title-order records together. At most 255 folders fit in a
512-byte table of boundaries. Larger tables use the sparse lookup. The row
buffer adds 384 bytes to the Saved Items activity.

| Cold index scan, including setup | Test75 | Test76 |
| --- | ---: | ---: |
| Calls to read the index | 8,799 | 4,516 |
| Simulated FAT sector reads | 6,150 | 4,641 |
| Simulated exFAT sector reads | 6,149 | 4,640 |

All 1,935 paths matched byte for byte in order. Neither scan wrote to the card.
The comparison covers the index scan. Checking bookmarks and clippings and
drawing the screen take additional time. First-open time on the X4 Pro is open.

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

## Loading Recents and other JSON stores

The default JSON store loader now reads and checks the full file in one
storage read. It keeps the 50,000-byte limit for Recents and other default
stores. Oversized files and failed reads or closes stop before parsing. The
existing reader uses one checked buffer and closes the file before JSON parsing.
This adapts the [CrossInk RC3 store-read change](https://github.com/uxjulia/CrossInk/commit/22adfc7ae01e6995e2aa1d68d3ed17d0161500c7).
Measure startup time on the X4 Pro.

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

## Page turns in a book with clippings

After test71, the highlight renderer skips font-width measurements for words
outside a portable clip unless the preceding word was highlighted. It still
measures the adjacent word so zero-width text preserves highlight spacing.
Page-based clips in the same chapter retain the full word count. Words outside
the chapter's clipped source range skip the per-word search when all of its
clips have source offsets. The change adds no buffer or persistent state.

A seeded host comparison against test71 produced the same horizontal highlight
positions and widths on 4,000 generated pages, including mixed legacy and
portable clips, out-of-order source offsets, and books with 1,024 clips.
Clipping-record lookups fell from 4,240,544 to 1,984,071 and text-width calls
from 143,644 to 72,726 in that workload. The comparison uses a host font stub.
Page-turn latency and panel refresh still need measurement on the X4 Pro.

After test72, the renderer records the first and last clip for the current
chapter during its existing scan. Each word checks only that interval.
Records from other chapters can still occur inside it. The change keeps
two indices and allocates no new buffer.

Across three backed-up stores, all eight book/chapter groups (14 clips total)
were contiguous. In 4,000 generated pages with clips grouped by chapter,
record lookups fell from 3,093,425 to 280,812. Across three more sets of
4,000 pages (mixed, scattered, and all in one chapter), every horizontal
highlight position and width matched test72. With all clips in one chapter,
both versions made the same number of lookups. Device timing is open.

When starting a touch selection, the reader first looks for a saved clip in
the current chapter. If all clips belong to other chapters, it skips the word
measurements used to detect a tap on a saved highlight. Overlapping clips
still select the first matching record in storage order.

## Reopening a book with many chapters

The X4 Pro uses the existing chapter lookup table to read the progress total
at the end of each 32-chapter window. It reads the table offsets in order,
then reads the totals in file order. The same vector holds both sets of
values in succession. The cache format and saved positions stay compatible.

On a host fixture with 5,000 chapters, opening the metadata cache took 480
file reads totaling 1,913 bytes. The previous loader took 20,014 reads
totaling 188,919 bytes on the same file. Both returned the same book size
of 80,088,000 bytes. The tests also compared each chapter's cumulative size
against the writer. With the production SdFat library and a simulated card,
a cold FAT open used 218 sector reads, down from 374. A contiguous exFAT
file used 217, down from 373. Interleaving writes to a second file gave 218
instead of 374 on both filesystems. Timing on the physical card is open.

## Saving a clipping

When a clip closes, the EPUB reader can redraw the same page while the chapter
is still indexing. Test72 compared the number of pages built so far with the
estimated chapter length saved to `progress.bin`. With 8 pages built and 50
estimated, 100 identical redraws made 100 progress writes in a host check.
The follow-up made one. It also saved a revised chapter estimate or a new
visible text offset after reflow, even on the same page. Failed writes are
retried on the next render. The on-card progress format stays the same.
CrossInk RC3 also [compares the estimated total on redraw](https://github.com/uxjulia/CrossInk/blob/0b939f41ec22c0d647a5c65ec5126141cbe7e565/src/activities/reader/EpubReaderActivity.cpp#L7397-L7406).

All 110 EPUB position files from the pre-test71 backup passed an in-memory
round trip through the test71, test72, and follow-up reader code. Each version
read the position and wrote the original bytes. Four additional fixtures
covered the older four-byte format, its end-page sentinel, and the six- and
ten-byte forms.

Returning from text selection can coincide with a queued screen refresh.
The follow-up keeps the reader's page and clipping list under the render lock
until the clipping store finishes saving or rolling back. The separate text
export runs after the lock is released.

Each saved clipping has a 156-byte prefix with its location, sync fields, and
text length. Writing the prefix in one call reduced storage writes from 1,808
to 208 when saving the 100th short clip in a host fixture. At the X4 Pro limit
of 1,024 clips, writes fell from 18,440 to 2,056. Both writers produced
byte-identical files. These counts measure calls into storage. Time spent
waiting for the SD card and display still needs a device measurement.

When rewriting an existing store, a 512-byte source buffer keeps short clips
from repeatedly evicting the output sector. Allocation failure uses direct
reads. With 99 existing 64-byte clips, saving the 100th on simulated FAT went
from 251 sector reads and 160 writes to 108 reads and 93 writes. On exFAT it
went from 254 reads and 154 writes to 111 reads and 87 writes. On both, the
rewritten file matched the previous writer byte for byte after remount.

The reader checks the temporary store's sync and close results before replacing
the prior copy. If either fails, the prior clippings remain available for retry.

If the card reports a failed rename, the reader discards cached text offsets.
Reopening the book loads the surviving primary. If that copy is unreadable,
Saved Items and the reader can read a valid backup. Editing and sync wait for
a writable primary. A save that reported failure may appear after reopening
if the card completed the rename.

Cleanup of leftover clipping files checks their allocation chains before
removing anything. The X4 Pro can check a pair of files up to 5 MiB each,
using up to 80 KiB of temporary PSRAM for large files. An unreadable chain,
overlap, or failed allocation leaves the files in place. The normal save path
with no leftovers skips this check.

### Reading clips after an interrupted save

An interrupted write of a sibling directory entry can make a strict lookup
fail even when the primary clipping file still opens. In that case the reader
compares the file's embedded book path with the open book and validates its
records. Saved Items uses the same path check for counts. A match allows
reading. Editing and syncing resume when the store reopens with a successful
normal lookup. A backup whose
directory entry cannot be verified must pass the same embedded path check.
A moved store can still have its old book path until the next save. Its
verified filename works normally. If its directory entry also becomes
unreadable, the old embedded path cannot establish its new identity.

In a simulated three-clip save on each filesystem, cutting power between
whole sector writes preserved every earlier passage. A second experiment
stopped individual sector writes partway through changed bytes. On exFAT,
all 1,739 snapshots reopened the old or new generation. On FAT, four of
1,883 snapshots left the primary inaccessible and the backup incomplete.
The reader keeps the files for recovery. It never promotes an uncommitted
temporary file to fill that gap.

### Saving on a full card

A full exFAT card could keep scanning its allocation bitmap indefinitely when
saving a clip or mounting the card. The pinned SdFat patch ends the search
after the last cluster. The reader can report a failed save, and the user can
free space and retry.

A host test mounted FAT and exFAT images with 0–256 free clusters and added,
deleted, or replaced a clip in stores of 3–1,023 clips. Of 300 edits, 206
succeeded immediately. All 94 that failed succeeded after space was freed.
Each reopened store contained the expected passages and revisions. A separate
regression checks writing the last free cluster, attempting another write,
remounting a full exFAT image, and writing again after freeing space.

## Filtering finished books

The Library reads completion history without opening the files that control
per-book tracking. A book can be finished even if tracking is off. A missing
book or unreadable history stays visible when finished books are hidden.

On copies of 1,935 catalog paths with 114 seeded histories, the filter found
the same 57 finished books in both versions. Sector reads fell from 267,979
to 167,703 on simulated FAT and from 880,903 to 544,771 on exFAT. Neither
run wrote to storage. The counts cover completion lookups across the catalog.
Elapsed time in the reader's Library is still unmeasured.

## Bookmarks, clippings, and positions after a move or interrupted save

On simulated FAT and exFAT, Move to Read preserved all 110 primary EPUB
positions from the pre-test71 backup under both native and legacy cache names.
All 440 runs reopened at the expected position. The 156 runs with a backup
position also preserved its exact bytes.

Separate power-cut tests replayed three backed-up clipping stores containing
14 clips in total. Across 468 add, delete, and edit runs, all 20,538 snapshots
taken between complete sector writes reopened with the previous or updated
contents. Of 1,014,665 snapshots including partial writes, 648 on FAT could
not reopen the store. Every readable snapshot returned the complete old or
new contents. No exFAT snapshot in this fixture was unreadable. These tests
used copies of the backup and simulated sector writes.


### Interrupted SD writes while saving a position

Test81 checks the staged EPUB or XTC record's byte count, sync,
file error, and close before replacing the saved position. A regression
injects each failure and retries with the same next page. In 144 FAT and
exFAT fixtures, this caught 447 additional staging failures and retained the
previous position in each. Across the separate remove and rename steps, 72
publication-stage faults in each build left no readable canonical position
after remount. The fixtures used absent or independently allocated temporary
files. A pre-existing temporary file sharing the canonical cluster chain can
still damage the original when reopened for writing. The saved format is
unchanged. The writer does not open the CrossInk backup.


### Repeated interruptions during clipping recovery

Three backed-up clipping stores were replayed on FAT and exFAT with short
and long directory entries. A second simulated power cut interrupted each
distinct changed byte prefix of the writes made during first-remount
recovery. All 43,776 second-cut snapshots that needed recovery writes
reopened the old or new clipping set, including its passages and anchors.
In 648 separate first-cut FAT snapshots the store was unreadable. Edits and
sync preparation stayed blocked.

### Comparing the native and RC3 position writers

A Mac fixture replayed four page saves from each of 110 archived positions
across six directory layouts on FAT and exFAT. The current native writer
issued 12 sector writes per turn on FAT and 9 on exFAT. The tagged RC3
two-slot writer issued one sector write on every third and fourth turn of
these runs. Earlier turns included allocating and growing the slots. Actual
card latency depends on the card and controller.

The RC3 reader returned the old or new position on all 167,156 first-cut
snapshots. A reader using only the native primary saw the newest position
after 2,640 of the 5,280 completed RC3 saves and the previous position after
the other 2,640. Test81 and test82 keep the native writer.

The RC3 reader also reopened the previous or requested position in all
11,324,461 simulated cuts during a second save after recovery. On 14,484
uninterrupted retries, however, the writer reported success while the
reader still opened the previous position. One reproducible case started
with a torn directory entry for the backup. Reading both slots after
remount found only the original primary. A separate prototype checked
the written slot by name, sequence, and fields. It caught all 612 misleading
successes in 8,004 synthetic retries. This comparison used private copies
of the pinned RC3 writer on the Mac.

### Retrying native progress saves after an interrupted rename

Test82 opens the temporary file without truncating it. A fresh,
empty file needs no directory lookup beyond the normal save. Before reusing a
nonempty temporary file, the writer checks its size and allocation and compares
its cluster chain with the primary and CrossInk backup when present. The check
can use up to 4 KiB of temporary memory. A lock covers the checks and the save.
An unreadable or shared chain blocks the retry and preserves the files for
inspection. The remove-and-rename interruption window still exists.

The same 5,559 interrupted card snapshots were given to test81 and the new
writer. A subsequent save reported success with the wrong position on 12
snapshots in test81 and zero with the new writer. The new checks declined 764
additional retries on the interrupted snapshots. Both builds left the primary
untouched on all 152 observed primary/temp aliases.

A separate uninterrupted-save comparison ran on FAT and exFAT. With a fresh
temp, both writers used a total of 1,392 sector reads and 259 sector writes
across the 24 fixtures. With an independent leftover temp, verification
increased reads from 1,413 to 7,184 across 24 fixtures. Each writer issued 269
writes. Neither experiment measures physical page-turn latency.

A separate read-only test replayed 110 backed-up positions with and without
an abandoned temp file on FAT and exFAT. Across 187,062 simulated cuts,
the primary could not supply a native record in 25,520 cases. A strict
lookup reported the primary missing in 5,280 of those. Every one contained
the intended next position in a single-cluster temp file. A torn FAT
write also produced a decodable primary with different bytes from either
expected position in 880 cases. The native format carries no checksum.
The [backup inspector](BOOK_SETTINGS.md#inspecting-a-backed-up-reading-position)
can show a staged position separately for manual comparison.

### Loading a cached EPUB page

The reader gets the page-table and visible-text-table locations from one
header read. It then seeks directly to those entries and the page. The same
format is used for complete and partly indexed chapters. A missing table,
invalid range, or short index read starts the existing cache rebuild. The
retry keeps the requested page number. The existing error screen appears
if attempts run out.

A fixture read 14,496 page markers and visible offsets from synthetic v84
chapters on FAT and exFAT. Both loaders returned the same values. FAT
sector reads fell from 50,777 to 43,313, and exFAT reads fell from 50,766
to 43,302. Of 112 runs, 92 required fewer reads and 20 were unchanged.
This measures the cache lookup with a four-byte payload. Device latency
and full-page decoding are separate measurements.

The page decoder also checks short reads of its scalar fields before using
them, including element tags and link coordinates. A host test truncated a
page containing a rule, drop cap, border, footnote, and link at every byte.

A second fixture used the page and text-block decoders and serialized image
metadata. It opened 4,352 synthetic pages with short or dense text, including
illustration references and Japanese reading annotations. Across FAT and exFAT,
sector reads fell from 98,850 to 94,222 (4.7%). Every expected word, annotation
position, and image path matched. This includes decoding the page records.
Displaying pixels on the X4 Pro takes additional time.

### Text and image cache records

Unannotated text writes up to 16 empty ruby annotation lengths at once.
Reading an empty annotation now ends after its length. Text and image
records check the byte count before using a field. A growing image path or
annotation checks the largest available heap block first. Another task
can allocate after that check.

All 24 generated v84 chapters matched test83 byte-for-byte. Across 4,352
decoded pages, calls to read file data fell from 3,713,856 to 2,707,456.
Calls to write file data during chapter creation fell from 1,356,000 to
1,089,984. Physical-sector operations in these disk images stayed at
94,222 reads during decoding and 34,445 writes during creation. On the
reader each file operation takes the storage lock. The wall-clock saving
depends on the reader and card.

A separate trial read several ruby lengths at a time. Seeking back when it
encountered a Japanese annotation increased sector reads. The firmware
reads each length in order.

In 48 additional fixtures on contiguous and fragmented FAT and exFAT files,
each sector read failed once in turn. All 59,127 failures declined the page.
A subsequent read of the same page after remount returned the expected words,
annotations, and image references in every case. These read attempts made
zero sector writes. This exercises decoding and rereading the same saved
bytes. The separate reader regression checks which page a rebuild requests.

During chapter layout, the reader can open a page already written to the
temporary cache. If it cannot restore the append position afterward, it stops
the build before another page can overwrite the earlier text.
