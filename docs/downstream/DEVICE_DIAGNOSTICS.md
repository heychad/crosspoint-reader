# Device diagnostics

On the X4 Pro, **Settings → System → Device diagnostics** shows free bytes, the largest free block, and the sum of each memory region's lowest free-byte mark since boot for internal RAM and PSRAM. Select **Refresh measurements** for a new sample. Region lows can occur at different times, so their sum is not necessarily a minimum total that the device actually reached.

The first memory sample is taken on entry. Refreshed samples include the
diagnostics activity's memory. Region low-water marks reset on reboot.

## EPUB render timings

Select **Record EPUB timings**, read ten pages, then return to Device
Diagnostics. Text, illustrated pages, and pages that needed chapter indexing each
show the median and longest time among the latest 16 samples. The number in
parentheses is the sample count. Pages that build an index go in the indexing
group, including pages with illustrations. Selecting **Record EPUB timings**
again stops recording and keeps the results. Starting another recording clears
them.

The timer starts when the EPUB render begins and ends after layout, page load,
and the renderer's refresh calls. It includes initial and repeated page renders.
Touch and button detection, time waiting for the render task, progress saves,
and background prefetch are outside this measurement. A rebuild can also redraw
the same page. Compare the same book, display orientation, refresh setting,
and pages before and after a change. Use the indexing row for a chapter that
actually needed a build.

Recording starts off after boot. The recorder uses at most 220 bytes of RAM
and writes no SD files. Display fit and timing on the physical X4 Pro remain
unverified.

## SD firmware update failures

The SD updater shows the same error code as the serial log. For example,
`WRONG_BOARD` identifies an image for another board, `BAD_SHA` a failed image
hash check, and `WRITE_FAIL` a failed write to the update slot. The updater
uses the same codes before confirmation and during installation. This adapts
the [CrossInk RC3 diagnostic](https://github.com/uxjulia/CrossInk/commit/35da2c236de450ce095daa3ae5f85c9844fed83f).

For a device check, select a separate one-byte `.BIN` in `zDev`. The updater
should show `TOO_SMALL` before confirmation. Back should leave the failure screen.

## Reading the timings over Wi-Fi

After a reading session, open Wi-Fi File Transfer and visit
`/api/diagnostics/epub-timings` at the address on the X4 Pro. Anyone on the
network who can reach the reader during File Transfer or Calibre Connect can
read these totals. Recording starts and stops on the device.

The reply includes the recording state and three groups named `text`,
`illustrated`, and `indexing`. Each has a `count`. Groups with samples also
report `medianMs` and `maxMs`. A group with zero samples has no timing values.
The readings stay available until the next recording or reboot. If the reader
returns HTTP 503 with `render_busy`, retry after one second.

Builds after test72 can also report `lastClipSave`. Select **Record EPUB timings**
before saving a clip. The field shows how long it took to save the clip to the
book's clipping file (`storeMs`) and append it to the text export (`exportMs`).
It excludes selection, time waiting for a redraw to finish before the save,
and panel refresh. Only successful saves produce a sample. If the renderer is
busy when a save finishes, the previous sample stays in place. Starting a new
recording clears it. The reply contains no book titles or passage text.
