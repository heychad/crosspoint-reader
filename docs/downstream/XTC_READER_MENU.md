# XTC reader menu

Confirm, the reader touch-menu gesture, and the mapped Menu action now open a
menu for XTC books, including books without chapters. It offers percentage
navigation, statistics, tracking, Quick Actions, controls, Library, and Home. Chapters
appear when available; the touch toggle appears on touch devices and keeps the
menu open after changing the setting. The List/Toolbar/Tabbed preference still
applies to EPUB; this XTC menu uses the shared list UI.

With `CROSSPOINT_EXPERIMENTAL_COMPLETION` enabled, the menu also offers Mark
finished or Mark unfinished when completion state is readable. The mapped
completion shortcut uses the same action. Both use the shared EPUB completion
transaction and retry dialog. A failed operation retries the original target;
recovery uses the existing journal. The default build keeps this control hidden.

Delete Book Cache confirms before removing `cover.bmp` and numeric
`thumb_<height>.bmp` files, including uppercase spellings on FAT. These are the
generated cache files written by the XTC renderer. It leaves progress,
statistics, bookmarks, preferences, directories, and unknown files untouched.
The open book stays at the same page. A failed removal or close offers Retry;
images already removed can regenerate. Cancel returns to the menu.

XTC is not eligible for the existing EPUB move-to-`/read` automation. If that
policy is enabled, Mark finished is hidden outside `/read`; marking unfinished
remains available. The action checks the state and policy again when selected.

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
format, worker, or framebuffer changes. The menu has fixed storage for eleven
items. The completion-enabled X4 Pro ELF reports a 1,968-byte menu object,
including 660 bytes of row storage. In this build, the statistics screen is
1,944 bytes, and the reader's statistics object is 456 bytes. The title string
and shared UI allocations are additional. New child activities use checked
allocation. An allocation failure preserves
the reader and returns to the menu when possible.
The shared retry dialog is 900 bytes before its strings and shared popup
allocations. It retains the reader; the menu is closed.
The cache scan uses a 64-byte filename buffer and the existing HAL file handles.

## Remaining parity

Statistics deletion is not yet included. It needs the fork's existing guarded
save and recovery paths. Nearby transfer remains
excluded by Chad's scope. This stage does not complete B7 or establish device
readiness.

## Verification

The full host run passed all 1,793 tests. Both XTC checks passed again after
adding the uppercase filename case. The completion-enabled
X4 Pro build passed in 104.48 seconds (112,972 bytes static RAM; 5,667,878 bytes
flash). It uses a local profile extending `env:x4pro` and preserves its flags,
adding `CROSSPOINT_EXPERIMENTAL_COMPLETION=1` and
`CROSSPOINT_EXPERIMENTAL_COMPLETION_PROMPT=1`; deferred automation is not enabled.
The final default X4 Pro build passed in 66.17 seconds (113,060 bytes static
RAM; 5,637,662 bytes flash).
Tests cover menus with and without chapters/touch,
Confirm and touch entry, cancellation, percentage bounds, shared-action
dispatch, allocation failures, one-shot tracking toggles in both directions,
failed saves, and unknown marker state. Rendering and physical input remain
unverified on the device.

Live-tracking tests cover off/on intervals, repeated unchanged settings,
book/global save failures with retries, unknown markers, mismatched cache
paths, global tracking-off, and the reader callback's loading fallback.
Completion tests cover finished/unfinished labels and targets, unreadable
state, the move-policy guard, render readiness, and the shared retry flow.
Cache tests verify preserved file contents, numeric thumbnail names, uppercase
names, missing caches, removal/close/lock/read failures, and confirmation
cancel/retry/allocation behavior.

At the next device test, open XTC books with and without chapters, enter the
menu by Confirm and touch, cancel a percentage jump, jump to 100%, and reopen
the book. Verify the page, chapter navigation, and controls return path. Toggle
tracking off, return to reading for a minute, turn it on, and read for another
minute. Verify that only the enabled interval adds reading time and that the
book stays at the same passage.

In a completion-enabled candidate, mark an XTC book finished and unfinished,
then reopen it to verify the state. With move-to-`/read` enabled, verify that
Mark finished is absent for an unfinished XTC outside `/read`.

Cancel Delete Book Cache and verify that nothing changes. Then confirm it,
reopen the book, and verify its page and statistics. Its cover should regenerate
when next requested by the library or sleep screen.
