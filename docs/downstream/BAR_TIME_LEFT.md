# Reader bar time remaining

The EPUB top and bottom bar slot menus include estimated time left in the book
and in the current EPUB section.
Estimates have a `~` prefix. A dash means tracking
is disabled, the required reading history is unavailable, or there is no
remaining estimate. The book estimate uses saved and accrued session time and
the EPUB's reported progress; it requires at least two minutes of tracked time.
The value updates on page turns; it is an approximation and can change with
layout, book structure, and reading pace. The slot does not change statistics
or reading positions. The section estimate multiplies remaining whole pages by
the book's recorded forward-page pace, including the latest eligible turn. It excludes time on the current page and
requires at least one pace sample and a completed section layout. An EPUB
section often matches a chapter, but books can split or combine chapters, so it
is not a guaranteed chapter estimate.

The settings keep the previous numeric slot identities and append the new
value. The X4 Pro must still be tested with an existing book and a new book,
including tracking off, both bars and all four orientations. Check that saved
positions persist after changing slots.
Older builds do not recognize the new slot value and may reset a custom bar
when loading its settings; keep the pre-update settings backup for rollback.
