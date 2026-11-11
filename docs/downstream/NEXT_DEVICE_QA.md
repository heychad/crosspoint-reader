# Next X4 Pro device test

Use the latest reviewed, hash-verified local candidate and its matching source
packet. Work through the checks one at a time. Nothing in this plan has been
run on the device yet.

## Before installing

- Back up and verify the reader's hidden `.crosspoint` folder, including reading
  positions, settings, bookmarks, and statistics; preserve
  `.crossink-stats-backup` if present. Keep the books on the card and a known-good
  X4 Pro firmware image available for rollback. Older firmware may clear the new
  File Transfer shortcut assignment and reset newer theme or sleep-screen
  choices; reconfigure those choices after returning to this build.
- Confirm the candidate's SHA-256 against its `SHA256SUMS`. Keep the reader
  powered through **Settings → System → SD Card Firmware Update**. Record the
  version shown under **Settings → System → About** after restart.
- If the reader fails to boot, reports a storage error, loses the expected book
  position, or behaves unexpectedly, stop and preserve the backup before trying
  another update or restoring files.

## First checks

1. Reopen an in-progress book and confirm the passage, page turns, touch, and
   frontlight. Close and reopen it once. Check that the position still matches.
2. In the custom bars, check the title, the top edge, both screen edges, and
   book and chapter progress. The filled line should reach the display edges
   when complete; the unfilled part stays invisible. Verify the result in each
   orientation you use.
3. Try side-button tap and hold, a touch gesture, Quick Lock, and the five-slot
   Quick Actions menu. Record any accidental page turn or failure to unlock.
   Restore any temporary test bindings afterward.

## Library and saved content

4. From an open EPUB, open **Browse Files** from Reader Menu. Return Home and
   reopen the book from Recents; confirm its position. Repeat from More.
5. From an open EPUB, open **Library** from Reader Menu. Browse by genre and
   series, return Home, and reopen the book from Recents; confirm its position.
   Repeat using **More → Library**, again returning Home and reopening the book.
   Check format and finished filters, compact rows, filenames, and the recent
   cover grid.
6. Open **Saved Items** from Reader Menu. Return Home and reopen the book;
   confirm its position. Repeat separately from More, a configured button, and
   a Quick Actions slot, reopening the book after each route. Open a native
   bookmark and an older CrossInk bookmark if available. Do not delete saved
   items during this smoke test.

## Statistics and appearance

7. On a disposable test book with tracking active, check Book, This device,
   All devices, and Charts and history. Set and clear a start date; set a finish
   date and confirm it does not itself mark the book finished. Reopen the book
   and confirm its progress and dates.
   Check recorded days for 7 and 30 days against the calendar on This device;
   the counts include today and reflect tracked history. A missing clock shows
   unavailable values, as does history shifted too far into the future.
   All devices cannot detect absent files; compare the source count to the
   devices expected. Detected unreadable sources may produce partial data.
8. Create two device-statistics backups and two backups for the test book.
   Leave the reader available afterward so the backup files can be verified.
   Interrupted writes can leave incomplete `.bin` files; preserve and do not
   rely on those files. After a failed or interrupted backup from an older build, its
   `.part` and `.bin` can share storage. Preserve both and do not delete either.
   The book backup does not contain reading position or the EPUB; restore and reset
   are not yet built.
9. Try the reading-statistics sleep screen and small/large UI scale. Wake,
   reopen the book, and check text and controls in the orientations you use.
10. In an EPUB, try Default, Balanced, and Light from the reader menu. Compare
   a styled table and image, turn pages, and verify the reading position after
   switching modes and reopening. Reset the book's settings and confirm it uses
   the global mode. Repeat in both menu styles. The simpler modes can change
   formatting; do not infer a speed improvement without measuring it.
11. Open developer diagnostics after repeated reader, keyboard, and menu
   transitions and record free internal/PSRAM memory and largest blocks. Report
   slowdowns, freezes, reboots, or display artifacts; do not infer a performance
   improvement from a successful build.
12. From Reader Menu, More, a configured Quick Actions slot, and a configured
    reader button, open File Transfer and confirm the mode chooser appears. Try a
    held button and confirm its release does not select a mode. Return and reopen
    the book after each route; check its position. When ready, join the network and
    confirm the web interface identifies the X4 Pro fork.
    Check existing plugins and protected-content controls without changing
    accounts or purchases.

Google Drive wireless sync, full completion management, automatic updates, and
other unfinished scope are not part of this test. Online firmware updates remain
disabled until the transport and firmware can be authenticated.

## Minimal Home

Select Settings > Display > UI Theme > Minimal Home. Check that the centered
cover, title, and author fit, the current book opens at the saved position by
touch and buttons, and each menu entry remains reachable. Check a book with no
cover, both UI sizes, leaving and returning Home, and switching back to the
previous theme. Check internal and PSRAM free and largest blocks before and
after first Home render, thumbnail generation, and repeated navigation. This
first layout does not yet provide the CrossInk Minimal dashboard or statistics.

## Dashboard Home

Select Settings > Display > UI Theme > Dashboard Home. Check the recent book's
cover, title, author, progress, reading time, and estimated time left. When a
statistic is unavailable, a dash should appear. Open the book by touch and
buttons, confirm its saved position, and return Home. Try a different book,
turn tracking off for a book, and confirm the menu remains usable. TXT and
Markdown progress and time left should show dashes. Record Home
loading time and internal and PSRAM free and largest blocks before and after
entry, thumbnail generation, and repeated navigation. If instrumentation is available, measure
loop-task stack high-water with a large EPUB and missing or legacy-only
statistics. Restore your settings.

## Reading summary sleep screen

Set Settings > Display > Sleep Screen to Reading summary and manually sleep
from a book with tracked statistics. Check the title, progress, reading time,
and estimated time left; wake and confirm the same reading position. Check
Inverted under Sleep Screen Cover Filter. Disable tracking and confirm the
default sleep screen appears, then restore your settings. Quick Resume on
Timeout can override the chosen screen for an inactivity timeout.

## Book statistics from Library

In Library > Recent, hold a previously opened book and an indexed book to open each row's
options. Choose Reading statistics, check the book title and available values,
change Tracking, and return to Library. Edit and clear the start and finish dates
on a disposable book with existing statistics, then reopen its statistics and
check that the dates and other values persisted. Confirm the list still works and
restore the prior Tracking setting. Global tracking must be enabled for a book's
tracking choice to take effect on its next session. Check opening
latency, internal and PSRAM free and largest blocks, touch and remapped button
release behavior, and retry after a failed open.

## Cover and reading summary sleep screen

Select Settings > Display > Sleep Screen > Cover and reading summary. Open a
book with tracked statistics and a generated Home cover, then sleep manually.
Check the cover, title, progress, time, and wake position. Try a book without a
compatible cached cover and confirm the text summary appears. Disable tracking
and confirm the default sleep screen; check the Quick Resume timeout override
and the Inverted filter. Restore your settings. Record sleep latency and memory
headroom.

## Home-button shortcuts

In Settings > Controls > Home Button Gestures, temporarily assign Browse Files, Library,
Saved Items, File Transfer, and Reading statistics. Exercise tap, double-tap,
and hold and check each destination opens once in an open book. The new choices
should do nothing on other screens; retain a separate Home gesture. With a reader
menu or end-of-book menu open, hold Home while closing it, then release; no
shortcut should fire on the page. A fresh Home contact should work. Check Quick
Lock and automatic page turning. Exercise EPUB and XTC and all four
orientations, then confirm the book's saved position. Restore your prior
bindings. Older firmware may reset these new assignments.

Open **Home Button Gestures** from both EPUB menu styles. In the action list, scroll to
the final entries and select File Transfer and Reading statistics; reopen the
list to confirm each selection. Cancel a change, return to the book, check its
position, and restore your prior bindings. Check fit and navigation in all four
orientations. Check Back and touch selection through both nested screens,
including a Quick Actions reassignment. With a large EPUB, record internal and
PSRAM free and largest blocks before, during, and after opening the picker.
Verify a changed binding survives a restart, and check a language with longer
labels before restoring the original settings.

## Legacy edge gestures

On a disposable test card, place a valid `/.crosspoint/settings.json` containing
`"leftEdgeUp": 1` and no `readerLeftEdgeUp` key. Reboot and check that the
left-up edge action is Increase brightness. Make a normal settings change, save, and
reboot to check the imported action persists. Repeat with
`"readerLeftEdgeUp": 0` present and confirm that the modern Default behavior value wins. The separate
CrossInk settings file is not imported by this stage.

## Section time remaining

With tracking on and a book that has recorded forward-page pace, assign
**Time left in section** to a reader bar. Check it shows an estimate after the current
section has finished laying out and updates as whole pages are turned. A slower
or faster pace can change the estimate. Check the dash when no estimate is
available. Test both bars and all orientations. A
section can differ from a table-of-contents chapter; restore the previous slot.
Check touch and button navigation in the slot list and record internal/PSRAM
free and largest blocks while the reader and settings screens are open.


## Title shelf cover grid

In Settings > Library, enable Title shelf cover grid while leaving Recent books
in list view. In Library > Title, browse by touch and buttons, reverse the sort,
search, and open a book; check that its cover, title, and opened book agree.
Check missing covers. Clear the search, hold a book, choose Browse groups, and
reopen a title group. Hold a search result and confirm the options omit Browse
groups. Check Delete opens a confirmation, then cancel it.
Verify Recent remains a list, then enable its grid independently and check both shelves. Test both UI
sizes and orientations you use, and restore your prior settings. Record shelf
opening and thumbnail loading times and internal/PSRAM memory and largest
blocks before and after browsing several pages.


## Library first on Home

Enable Settings > Library > Library first on Home. On each Home theme,
check that Library precedes Browse Files and that both labels or icons open
the correct destination by touch and buttons. Check returning from each screen
leaves the matching Home item selected. Repeat with the setting off and restore
your preference.


## Statistics from any shelf

In Title, Author, Series, and Genre, hold an indexed book and select Reading
statistics. Confirm the book title and available values, return to the shelf,
and check selection and navigation. Try a filtered search and the Title cover
grid. With no search active, choose Browse groups and reopen a group. Confirm
Delete opens a confirmation and cancel it. Check touch and button holds, memory
headroom, and repeated entry. During repeated redraws, confirm the Delete
confirmation shows the selected book title, then cancel it. Record group-browsing
latency with the full library. Restore any changed tracking setting.

## Settings recovery

Use a disposable test card with known settings and recovery files. Check the
warning, Retry, and OK in Home and Settings, then trigger a failed save while
reading and check its warning after the next repaint. Repeat in portrait and
landscape at both interface sizes. Check the web error while File Transfer is
open, then leave File Transfer and check the reader warning. Verify a successful
retry, and preserve the files when retry cannot resolve the state. Measure save
time and free and largest internal and PSRAM blocks with a book open.

## Home control and font shortcuts

In an EPUB, toggle Home Button in Reader off and on. Confirm its saved tap,
double-tap, and hold mappings remain intact, that Home works in reader menus,
and that Quick Lock remains unlockable. Open the font picker and cycle through built-in and
installed SD fonts using their shortcuts. Close and reopen the book, then check
its font and reading position.

## Closed-book date changes on exit

Using a disposable book, edit a date in Library statistics and immediately exit
or sleep. Reopen statistics and the book to check the date and reading position.
Record exit and sleep latency, watchdog behavior, task stack high-water marks,
and free and largest internal and PSRAM blocks.

## Next candidate: chapter shortcuts

After the next reviewed firmware is installed (not this WIP checkpoint):

1. Assign Select Chapter to a Quick Actions slot or Home-button gesture. In an
   EPUB, open it and cancel. The book should return to its previous position;
   opening the same picker from the reader menu should return to that menu.
2. Select a named chapter. Confirm its opening text and that sleep/reopen
   preserves the new position. Repeat with a configured two-finger swipe.
3. In an XTC with a table of contents, the shortcut should open its chapter
   picker. The Quick Actions picker should hide it for an XTC without chapters.

No book files, statistics formats, or saved action IDs are migrated by this
feature. Existing bindings should continue to perform the same actions.

For the RC mapping follow-up, verify Home/Reader returns between the reader and
Home, Rotate 180° preserves the current content position, and toggling the Home
button off can be reversed through Settings. Existing bindings must retain
their actions after restart. Auto-turn and Focus actions reuse their existing
behavior; their broader feature parity remains in the Later scope.
