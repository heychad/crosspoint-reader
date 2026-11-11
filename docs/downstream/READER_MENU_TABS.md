# Tabbed reader menu

Settings > Reader > Reader menu > Tabbed adds five categories to the EPUB
reader menu: Font, Layout, More, Location, and Settings. The original List and
Toolbar choices remain available. Existing saved values 0 and 1 keep their
meaning; Tabbed is value 2. Older firmware may reset that unknown value.

The menu uses the existing shared tabbed-list component. Touch selects tabs;
button navigation includes the tab band, where Confirm advances the tab.
Each tab remembers its row and viewport while the menu is open. The last
selected action chooses the tab when the menu reopens.

Font has direct line, word, and character spacing, anti-aliasing, and Focus
Reading controls. Font family and size open scrolling panes inside this menu;
Back returns to the previous Font-tab position. Family names come from the
existing registry, and sizes come from the selected family's supported sizes.
Layout has direct margins, alignment, image mode, hyphenation, paragraph
spacing/indentation, and embedded-style controls. It also retains orientation
and rendering-mode controls. More
contains chapter, percentage, lookup, and automatic page-turn actions.
Location contains bookmarks, clippings, Saved Items, screenshots, and QR.
Settings contains the remaining device, navigation, statistics, and reset
controls. Availability checks are shared with the List menu.

Per-book tracking is available from the reader menu and toolbar More panel.
It uses the shared statistics screen and applies when returning to reading.
Statistics-restore-enabled builds also offer Delete book statistics, using the
same confirmation, backup, session, and recovery flow as XTC. Cancel returns
to the appropriate reader menu. List, Toolbar, and Tabbed retain their layouts.

The 12 inline controls stay in the reader menu. Choices use the existing popup;
toggles apply immediately. An unchanged choice does not write settings. Each
change uses the reader's existing text-offset preservation, pagination reset,
font loading, and per-book preference save path before saving global settings.
Closing the menu then rebuilds the book at the saved text position. Existing
save warnings still apply. The parent reader remains alive while the menu owns
its callback. A failed menu allocation leaves the reader active.

Font choices apply immediately through the same reading-position and save path.
An unchanged selection does not write. If the registry changes while a pane is
open, the pane closes and consumes that input frame; reopening refreshes the
list. Names that cannot fit the existing saved font-name field are disabled
instead of truncated. The panes do not add a live book-text preview.

The [RC audit](EPUB_MENU_AUDIT.md) lists the remaining drawer work and separates
Later/Skip controls. The separate [XTC menu](XTC_READER_MENU.md)
now includes navigation, live tracking, completion-gated finished status, and
state-preserving image-cache reset. Statistics deletion is available behind
the statistics-restore flag, with session coordination and backup.
Later-scope dictionary/font-engine
features are not added.

Root rows reuse fixed storage for at most 46 actions with a 46-byte index array.
The X4 Pro ELF reports a 4,872-byte menu object.
The menu retains its 40-byte saved navigation record and 12-byte size-vector handle.
The menu-item vector reserves 368 bytes; popup strings and shared UI allocations
are additional. Font panes use the shared row provider instead of copying a
row per family. Names stay in the registry; size labels use one 12-byte buffer.
The size vector contains at most 256 distinct byte-sized points from the
existing helper. A saved navigation record restores the Font tab. Popups have
at most eight options here. The parent reader is retained; its section is
released on the first text edit. No background task, framebuffer, or storage
format is added. Runtime heap and input/rendering behavior need device checks.

For the next device batch, open Tabbed, switch across all five categories by
touch and buttons in all four orientations, and change font size and layout. Reopen the book and verify
its passage. Then switch back to List and Toolbar and verify both still open.

Host checks cover all 12 controls, option bounds, unchanged choices, repeated
edits, cached-position preservation, and the unchanged List/Toolbar menu.
The tab test exercises the maximum 46-row configuration with completion and
statistics restore enabled.
Font-pane tests cover an 80-family registry, sparse sizes, Back restoration,
unchanged selections, rejected long names, dirty-registry handling, and remembered
menu actions. The current full host run passed all 1,806 tests; focused checks
also cover both new actions in the menu and More panel. Hardware rendering and
loading remain unverified.

The default and restore-enabled X4 Pro builds passed in 86.17 and 76.03 seconds.
