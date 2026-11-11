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
Reading controls. Font family and size still open their existing pickers.
Layout has direct margins, alignment, image mode, hyphenation, paragraph
spacing/indentation, and embedded-style controls. It also retains orientation
and rendering-mode controls. More
contains chapter, percentage, lookup, and automatic page-turn actions.
Location contains bookmarks, clippings, Saved Items, screenshots, and QR.
Settings contains the remaining device, navigation, statistics, and reset
controls. Availability checks are shared with the List menu.

The 12 inline controls stay in the reader menu. Choices use the existing popup;
toggles apply immediately. An unchanged choice does not write settings. Each
change uses the reader's existing text-offset preservation, pagination reset,
font loading, and per-book preference save path before saving global settings.
Closing the menu then rebuilds the book at the saved text position. Existing
save warnings still apply. The parent reader remains alive while the menu owns
its callback. A failed menu allocation leaves the reader active.

Full inline font-family/size panes, other RC drawer features, and XTC menu parity
remain unfinished. Later-scope dictionary/font-engine features are not added.

Rows reuse fixed storage for at most 44 actions with a 44-byte index array.
The X4 Pro ELF reports a 4,680-byte menu object, including 2,640 bytes of row
storage and the 16-byte callback. The menu-item vector reserves 352 bytes;
popup strings and shared UI allocations are additional. Popups have at most
eight options here. The parent reader is retained; its section is released on
the first text edit. No background task, framebuffer, or storage format is
added. Runtime heap and input/rendering behavior need device checks.

For the next device batch, open Tabbed, switch across all five categories by
touch and buttons in all four orientations, and change font size and layout. Reopen the book and verify
its passage. Then switch back to List and Toolbar and verify both still open.

Host checks cover all 12 controls, option bounds, unchanged choices, repeated
edits, cached-position preservation, and the unchanged List/Toolbar menu.
The tab test also exercises the maximum 44-row configuration with completion
enabled. The default X4 Pro build passed in 71.87 seconds. The full host run
passed 1,785/1,786 tests; its older statistics-menu fixture then gained the shared
control table, and all three affected menu tests passed, including the fixture's
three completion configurations. No production edit followed the build.
