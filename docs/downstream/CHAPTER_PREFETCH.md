# Silent next-chapter prefetch

The selected G2 core uses the existing render task to prepare the next EPUB
section while the current page stays visible. Enable
`CROSSPOINT_EXPERIMENTAL_CHAPTER_PREFETCH=1` in an X4 Pro WIP profile. The default
profile leaves it disabled. This is not an installation candidate.

The tagged [CrossInk RC2 reference](https://github.com/uxjulia/CrossInk/blob/959e3d1575cf1cf49475c4c4546e49adadf4d477/src/activities/reader/EpubReaderActivity.cpp)
prepares the next section through its one-shot builder, cancellation options,
and render-profile fallbacks. This port uses CrossPoint 2.0.0's existing
incremental builder, render-task dispatch, HAL memory checks, and partial-cache
cleanup. It preserves the current reading settings during optional work.

## Scheduling and cancellation

The reader arms work within the final two pages of a completed section, after
400 ms without a new paint. Held buttons, touch contact, queued page turns,
overlays, an active current-section build, Bluetooth heap ownership, and the
last spine prevent admission. Arming and execution both check memory.

The render task owns the temporary `Section` under `RenderLock` and the existing
power lock. A normal redraw wins over background work. Physical, touch, remote,
and tilt input update the existing cancellation sequence before reader dispatch.
The builder checks it during extraction and between parser steps. A changed
sequence, memory pressure, or Bluetooth heap ownership ends work.

A five-second work budget starts after probing the next section's cache. It is
checked at the same extraction/parse boundaries. This is a cooperative limit:
an individual I/O operation, layout callback, cache probe, or suspension write
can take longer. It does not guarantee a five-second wall-clock response.
Between unfinished page-build calls, `delay(1)` gives other tasks time to run.

There is one attempt from a given current spine. A valid complete next-section
cache needs no rebuild. Cancellation or the time budget releases the temporary
builder; its existing destructor suspends useful partial work. The foreground
reader can later use those pages and finish the chapter normally. The prefetch
does not change reading position, settings, statistics, plugin events, or the
native section-cache format.

## Memory and display ownership

| Check | Default-capability free / largest block | Internal free / largest block |
| --- | --- | --- |
| Admission | 96 KiB / 48 KiB | 32 KiB / 16 KiB |
| During work | 32 KiB / 16 KiB | 16 KiB / 8 KiB |

Admission reserves a block larger than the inflater's approximately 43 KiB
scratch allocation. Internal checks prevent abundant PSRAM from masking low
internal RAM. These are conservative admission policies, not measured peak
headroom or a guarantee against allocation failure inside a parser step.

No task, second framebuffer, or retained parser is added. The enabled reader
adds one four-byte start timestamp to its existing prefetch state. The temporary
section coexists with the current section only during the background call and
uses the existing checked builder allocations and HAL file ownership. Prefetch
does not lend the display framebuffer. The existing loan-count guard requests a
normal redraw if a nested operation unexpectedly changes it.

## Verification and device check

The production scheduler methods run in a host harness with controlled input,
heap, clock, radio, and section adapters. It covers admission, cache reuse,
partial work, cancellation, timer wrap, memory/radio pressure, yielding,
start failure, and unchanged current position/display. Render-task dispatch has
a separate production-method harness. Section cache recovery, parser-error and
write-error preservation, and patched FAT/exFAT tests cover the unchanged cache
cleanup contract. Host adapters do not establish device latency or heap peaks.

Seven focused checks pass in 5.51 seconds. The scheduler, dispatch, and expanded
destructor-suspension harnesses pass AddressSanitizer and UndefinedBehaviorSanitizer.
The default X4 Pro build passes in 72.88 seconds (RAM 113,148 bytes; flash
5,683,694 bytes). The eleven-flag combined profile passes in 160.20 seconds
(RAM 113,092 bytes; flash 5,783,570 bytes). Compiled `EpubReaderActivity` size is
3,016 bytes in default and 3,328 bytes in the combined profile; that difference
includes its other enabled features and is not the prefetch cost alone.
The compiled prefetch method has a 96-byte stack frame; that excludes the
builder/parser/font call chain and does not establish render-task headroom.

For the next reviewed device candidate, open an uncached multi-section EPUB
near a section boundary. Wait briefly, then cross the boundary. Confirm there
is no indexing popup or idle flash. Repeat with an immediate page turn, menu,
Quick Lock, and sleep/wake; verify the passage and reading position. Measure
internal/default/PSRAM heaps, largest blocks, render-stack headroom, and first
versus repeated boundary timing with text and image-heavy books. Cumulative
review and these physical measurements remain required.
