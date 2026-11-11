# XTC reader menu

Confirm, the reader touch-menu gesture, and the mapped Menu action now open a
menu for XTC books, including books without chapters. It offers percentage
navigation, statistics, tracking, Quick Actions, controls, Library, and Home. Chapters
appear when available; the touch toggle appears on touch devices and keeps the
menu open after changing the setting. The List/Toolbar/Tabbed preference still
applies to EPUB; this XTC menu uses the shared list UI.

Percentage navigation reuses the existing picker. It rounds the one-based page
position and clamps it to the book's pages: 100% selects the final bitmap, not
the end-of-book screen. Cancel returns to the menu without changing position.
Chapter selection follows the same return behavior when opened from the menu;
the existing direct chapter shortcut still returns to reading.

The tracking action opens Statistics and requests one toggle after the saved
tracking markers have been read. It selects the tracking row so the chosen
state or failure is visible. Unknown marker state never triggers a write.
When this screen closes, the reader applies the saved setting to its current
session. Turning tracking off first saves accrued time; turning it on starts
a fresh tracked interval. Time spent reading while disabled is not counted.
Global tracking-off still takes precedence. Failed writes retain the existing
Check again action. If accrued time cannot be saved, the live session remains
available for retry and the reader shows its statistics-save warning.
No statistics or reading position are deleted by this toggle. Opening Statistics
through its ordinary action retains the existing next-open setting behavior.

Statistics and Quick Actions use existing reader handlers. Controls uses the
existing settings activity. No progress format, bitmap renderer, book-cache
format, worker, or framebuffer changes. The menu has fixed storage for nine
items. The X4 Pro ELF reports a 1,832-byte menu object, including 540 bytes of
row storage. The retained statistics screen is 1,940 bytes, and the reader's
statistics object is 456 bytes. The title string
and shared UI allocations are additional. All
new child activities use checked allocation. An allocation failure preserves
the reader and returns to the menu when possible.

## Remaining parity

CrossInk's completion, statistics deletion, and cache deletion are not yet
included. They need the fork's existing guarded
save and recovery paths. Nearby transfer remains excluded by Chad's scope. This stage
does not complete B7 or establish device readiness.

## Verification

The full host run passed all 1,792 tests. Five statistics-screen tests passed
again after updating the tracking labels. The final X4 Pro build passed in
66.46 seconds (113,060 bytes static RAM; 5,636,734 bytes flash).
Tests cover menus with and without chapters/touch,
Confirm and touch entry, cancellation, percentage bounds, shared-action
dispatch, allocation failures, one-shot tracking toggles in both directions,
failed saves, and unknown marker state. Rendering and physical input remain
unverified on the device.

Live-tracking tests cover off/on intervals, repeated unchanged settings,
book/global save failures with retries, unknown markers, mismatched cache
paths, global tracking-off, and the reader callback's loading fallback.

At the next device test, open XTC books with and without chapters, enter the
menu by Confirm and touch, cancel a percentage jump, jump to 100%, and reopen
the book. Verify the page, chapter navigation, and controls return path. Toggle
tracking off, return to reading for a minute, turn it on, and read for another
minute. Verify that only the enabled interval adds reading time and that the
book stays at the same passage.
