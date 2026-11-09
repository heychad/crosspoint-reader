# EPUB rendering profiles

Choose Default, Balanced, or Light in Settings > Reader or the reader menu.
The in-book choice applies to that book. Reset book settings to return to the
global default. Changing the profile rebuilds the chapter layout and tries
to preserve the current text position.

Default uses the existing CrossPoint rendering. Balanced retains CSS selector matching and stacks table cells. Light also removes publisher layout and
decoration, including margins, borders, and image sizing. Both simpler modes
can look different from the publisher's design. All modes retain contextual CSS selector matching, including rules that hide text.
Use Default when publisher styling matters.

Changing modes invalidates chapter layout caches. The existing reading-position
and clipping mechanisms retain their separate identities; non-default layouts
use different clipping signatures. This does not include automatic low-memory
fallback, CrossInk preference import, or Safe Mode recovery. The extra mode
adds one byte to each preference snapshot and section header, plus parser code.
Device heap, rendering time, and reading-position behavior remain unmeasured.

On the X4 Pro, compare styled tables, images, hidden text, and footnotes in all
three modes. Change modes and reopen the book to check its position, per-book
setting, and reset behavior. Test both reader menus and the orientations used
for reading. Report freezes, reboots, missing text, or layout errors.
