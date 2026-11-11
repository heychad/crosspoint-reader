# Live reader preview

The tabbed reader menu shows a reflowed excerpt on its Font and Layout tabs,
including the font-family and size panes. It uses the existing Text Settings
preview and reader text-layout engine. Changing font, size, spacing, margins,
alignment, indentation, focus reading, or hyphenation refreshes the preview.
This does not simulate images, publisher CSS, or grayscale antialiasing.

Opening the menu takes at most 1,024 bytes from the current page, stopping at a
whole-word boundary and retaining paragraph breaks. It releases the temporary
page before allocating the menu. The menu owns that excerpt; it does not borrow
objects from the reader's section, which settings changes can release. A page
without extractable text uses the existing sample text.

The preview keeps at most 24 laid-out lines, including paragraph separators.
It releases those lines on the other tabs and rebuilds them when returning.
The cache key includes the excerpt and font-family identity, so swapping fonts
that share a renderer ID still refreshes the text. No new task or framebuffer
is added. Heap use depends on the selected font and excerpt; device measurement
remains necessary.

The preview reserves space before the settings list, keeps room for two rows,
and clips drawing to its bounds. Other tabs retain their previous list space.
The existing List and Toolbar modes continue using the Text Settings screen.

For the next device test, open a text page, switch font and size in the tabbed
menu, adjust spacing and margins, and check that the excerpt refreshes. Close
the menu and confirm the same passage. Repeat in both landscape orientations,
with an SD font, and with an image-only page. Check that popups and touch targets
stay clear of the preview.

Validation: all 1,833 host tests passed in 68.53 seconds, including production
preview layout at both UI scales, both themes, and all four orientations.
The default X4 Pro firmware built in 127.59 seconds. The menu object is 5,204
bytes, including a 48-byte preview layout (36-byte key); excerpt, line, and
font-cache allocations are additional. These checks do not establish runtime
heap headroom or physical display behavior.
