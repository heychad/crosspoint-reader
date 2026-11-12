# Page Overlay reconstruction

The experimental Page Overlay can now reconstruct an EPUB, TXT, or Markdown
background when its section is missing or no longer matches the reading
layout. It extends the live-book path rather than copying CrossInk RC2's
separate book-opening and settings-migration implementation.

## Behavior

The reader first tries its current materialized page. Otherwise it uses the
loaded book and current effective settings to validate or rebuild a temporary
section. An available source-text offset locates the passage after reflow;
legacy data uses its existing chapter-page index, clamped to a shorter rebuilt
chapter. Invalid chapter positions and end-of-book state are rejected.

The builder advances in the normal eight-page chunks until the target is
available, yielding between chunks. It does not finish the rest of a chapter
unless the target requires it. Destruction persists unfinished derived pages
through the existing partial-cache writer and releases the parser before fonts
and images are prepared for drawing. No new cache format is introduced.

Memory failures can retry Default, Balanced, Light, then Safe as supported by
the existing recovery helper. These choices are local to sleep reconstruction;
they do not change per-book preferences, global settings, or the reader's live
recovery state. I/O failures do not trigger memory-profile retries.

An active section builder keeps exclusive ownership of its files and parser.
Its materialized pages still use the fast path; a failed or stale active-build
page uses the existing default-screen fallback rather than starting a second
builder. Other failures, absent fonts, and unavailable framebuffers also keep
the existing fallback. Quick Resume retains priority.

## State and resources

The original reader position and section object remain in place for normal
exit/session/plugin handling. Reconstruction does not write progress or reset
reader settings. It shares the loaded EPUB, including protected-book access;
it does not open another book or duplicate activation policy.

A temporary, checked Section owner is added only after the normal radio-build
handoff and heap admission. Its derived parser and page LUT use the existing
incremental builder. The guard requires at least 32 KiB free and a 16 KiB
largest block in the default allocation pool before allocation and each build
chunk. This is an admission threshold, not a measured peak-memory bound.
The previous modal page snapshot is released before reconstruction. In the
X4 Pro build, Section is 84 bytes, the existing build-context object is 188
bytes, its parser object is 1,668 bytes, and the local recovery state is eight
bytes. These sizes exclude the existing builder's dynamic text, CSS, page,
and LUT storage; they are not a peak-heap measurement. There is no new task, full-screen buffer, persistent setting, or sidecar file.

Internal/PSRAM peaks, largest blocks, and sleep latency still require X4 Pro
measurement with representative books and modal screens. A cache rebuild can
make the first sleep slower; later sleeps can use its derived pages.

## Verification

Host checks execute the production reconstruction method and render-recovery
helper with ASan/UBSan. They cover source offsets, page-index fallback, complete
and partial caches, reaching only the requested passage, unavailable memory or
radio, allocation/read failure, bounded recovery, active-builder exclusion,
and unchanged reader position/preferences. The existing sleep harness checks
routing into reconstruction, all four orientations, loaded-page rendering,
Quick Resume priority, and parent activity lifetime. XTC/XTCH retain their
existing direct-page path.

Cumulative release review and device checks remain. At the next reviewed
handoff, open a book, change its layout, and sleep before the new layout has
painted. Compare the passage beneath the overlay, wake, and verify the saved
position. Also check a large partially indexed chapter and sleep from a reader
modal. Store manually staged test artifacts under `/zDev/`.

The 13 affected checks passed in 2.37 seconds. The full configured host suite
passed 2,282 checks in 51.48 seconds. A strengthened direct rerun also passed
with failures injected during build chunks. The enabled X4 Pro profile
`x4pro-completion-wip` passed in 75.30 seconds, using 113,060 bytes of static
RAM and 5,712,986 bytes of application flash. Logs are
`/tmp/x4-sleep-rebuild-focused.log`, `/tmp/x4-sleep-rebuild-all-tests.log`,
`/tmp/x4-sleep-rebuild-host-final.log`, and `/tmp/x4-sleep-rebuild-pio-final.log`.
Pre-existing SDK edits remain unchanged and excluded from this checkpoint.

The broader E2 feature remains partial. RC2 adds a Cover + overlay mode that
is not imported yet, and its separate Minimal statistics screen currently maps
to Minimal book in this fork. Those are the next sleep-mode gaps; they are not
counted as completed by this reconstruction stage.
