# Per-book tracking

In an open book, choose Reading Statistics and select **Per-book tracking setting** to change it. The value is the setting for the next time that book opens. The current reading session keeps its existing tracking state until the book closes. The global reading-statistics switch must also be on for new sessions to be tracked.

The setting does not erase reading statistics or reading positions. It uses CrossInk's existing disable markers, including legacy EPUB cache locations. If any marker cannot be checked, the screen shows a failure and does not attempt a change. A failed enable can leave some markers removed and tracking still off; select the row to check the state, then retry. A failed disable may leave a marker in place; check the state before trying again. Missing or damaged storage needs attention before changing the setting.

When opening a book, an unreadable tracking marker or uncertain statistics-file lookup leaves statistics tracking off for that session. Book-state moves also stop if the tracking setting or statistics state cannot be checked. Statistics recovery treats an unreadable file or a failed read-close as unavailable instead of using an older file or starting with empty totals.

The initial check and changes access the SD card on the activity loop after a repaint. Input waits for this work. On-device SD latency and behavior during card faults remain to be tested.
