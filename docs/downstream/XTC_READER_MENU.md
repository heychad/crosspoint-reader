# XTC reader menu

Confirm, the reader touch-menu gesture, and the mapped Menu action now open a
menu for XTC books, including books without chapters. It offers percentage
navigation, statistics, Quick Actions, controls, Library, and Home. Chapters
appear when available; the touch toggle appears on touch devices and keeps the
menu open after changing the setting. The List/Toolbar/Tabbed preference still
applies to EPUB; this XTC menu uses the shared list UI.

Percentage navigation reuses the existing picker. It rounds the one-based page
position and clamps it to the book's pages: 100% selects the final bitmap, not
the end-of-book screen. Cancel returns to the menu without changing position.
Chapter selection follows the same return behavior when opened from the menu;
the existing direct chapter shortcut still returns to reading.

Statistics and Quick Actions use existing reader handlers. Controls uses the
existing settings activity. No progress format, bitmap renderer, book-cache
format, worker, or framebuffer changes. The menu has fixed storage for eight
items. The X4 Pro ELF reports a 1,764-byte menu object, including 480 bytes of
row storage; the title string and shared UI allocations are additional. All
new child activities use checked allocation. An allocation failure preserves
the reader and returns to the menu when possible.

## Remaining parity

CrossInk's direct tracking, completion, statistics deletion, and cache deletion
controls are not yet included. They need the fork's existing guarded save and
recovery paths. Nearby transfer remains excluded by Chad's scope. This stage
does not complete B7 or establish device readiness.

## Verification

The full host run passed 1,787 of 1,788 tests; the remaining ChapterShortcut
fixture used the previous method signature. After updating that fixture,
ChapterShortcut and XtcReaderMenu both passed. The final default X4 Pro build
passed in 34.61 seconds. Tests cover menus with and without chapters/touch,
Confirm and touch entry, cancellation, percentage bounds, shared-action
dispatch, and allocation failures. Rendering and physical input remain
unverified on the device.

At the next device test, open XTC books with and without chapters, enter the
menu by Confirm and touch, cancel a percentage jump, jump to 100%, and reopen
the book. Verify the page, chapter navigation, and controls return path.
