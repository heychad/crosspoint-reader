# Book statistics from Library

On any Library shelf, hold a book and choose Reading statistics to open the
existing book statistics screen. This includes pinned recents, indexed rows,
and cover grids. On other shelves, the options also include Open and Delete;
Browse groups appears when no search is active. Delete asks for confirmation. Per-book Tracking can be changed for the next reading session;
the global tracking switch must also be enabled. Editing start and finish dates
still requires an actively tracked open book.

The launcher closes the Library index while it reads saved book/global data and
opens the child screen. It reopens the index on return; an index reopen failure
shows the existing library error state. Missing, unreadable, or unknown book
statistics show as unavailable rather than zero. Device totals are read from
the primary global file only; a recovery copy may exist even when this view
shows them as unavailable, and opening the view does not repair it. TXT and
Markdown progress is
left unavailable to avoid cache validation during this read. If the launcher
cannot open the book or allocate the screen, it returns to Library and shows an
error so the user can retry.

The temporary snapshot is heap-owned and bounded to 512 bytes, and is released
before the child screen opens. Library remains on the activity stack while the
statistics screen is active. No task or network connection is added. The
statistics screen's existing tracking and backup actions can write only when
chosen by the user. SD latency, combined activity heap use, and input behavior
need X4 Pro device checks.
