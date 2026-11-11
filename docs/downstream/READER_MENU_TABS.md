# Tabbed reader menu

Settings > Reader > Reader menu > Tabbed adds five categories to the EPUB
reader menu: Font, Layout, More, Location, and Settings. The original List and
Toolbar choices remain available. Existing saved values 0 and 1 keep their
meaning; Tabbed is value 2. Older firmware may reset that unknown value.

The menu uses the existing shared tabbed-list component. Touch selects tabs;
button navigation includes the tab band, where Confirm advances the tab.
Each tab remembers its row and viewport while the menu is open. The last
selected action chooses the tab when the menu reopens.

Font opens the existing font-family or size screen. Layout opens the layout
or style screen and retains orientation and rendering-mode controls. More
contains chapter, percentage, lookup, and automatic page-turn actions.
Location contains bookmarks, clippings, Saved Items, screenshots, and QR.
Settings contains the remaining device, navigation, statistics, and reset
controls. Availability checks are shared with the List menu.

This is category and navigation parity, not a replacement for CrossInk's full
inline drawer controls. XTC's separate menu is unchanged. Text-setting actions
use the existing reader-position preservation and preference save path; a
failed screen allocation returns to the menu.

Rows reuse fixed storage for at most 34 actions. Filtering adds a 34-byte
index array; the shared tab host retains five navigation records only when
Tabbed is selected. There is no new background task or storage format.
Device memory, rendering, touch behavior, and timing remain to be checked.

For the next device batch, open Tabbed, switch across all five categories by
touch and buttons in all four orientations, and change font size and layout. Reopen the book and verify
its passage. Then switch back to List and Toolbar and verify both still open.
