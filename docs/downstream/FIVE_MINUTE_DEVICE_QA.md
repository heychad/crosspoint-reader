# Five-minute X4 Pro check

Run after installing the reviewed clipping repair candidate. Keep this check at
five minutes. Report the failed step, book, and action; a pass is `Passed 1–6`.

`CLIPFIX1.BIN` failed the device test. Wait for the reviewed replacement before
running this checklist. Firmware images are staged under `/zDev/`; open `zDev`
in **Settings → System → SD Card Firmware Update**. Keep the reader powered
through the restart, then start the timer.

| Time | Check | Pass |
| --- | --- | --- |
| 0:00–0:30 | Check About, then reopen your in-progress book. | Repair version is shown; the passage matches where you stopped. |
| 0:30–1:30 | Long-press book text to open clipping selection. Drag each handle across several lines, extend and shrink the selection, then save. | Controls appear without a reboot; selection follows the handles; saving returns to the book. |
| 1:30–2:15 | Open Saved Items and inspect the new clipping. | The expected words and book are present, without duplicates. |
| 2:15–3:15 | Reopen the book. Drag selection to the bottom and hold briefly to extend onto the next page, then cancel. Open, adjust, and cancel twice more. | Page extension and all three cancellations work without saving or crashing. |
| 3:15–4:15 | Turn five pages forward and five back. Try Quick Lock and unlock. | Buttons and touch work; unlocking does not turn another page. |
| 4:15–5:00 | Sleep, wake, and reopen the book. Then reopen Wi-Fi File Transfer. | The passage resumes correctly, and the web page is reachable. |

If it reboots, stop the check, reopen File Transfer, and leave it connected.
Preserve `crash_report.txt`; do not delete it or clear the book's cache. Record
which step failed and whether the crash happened on opening selection,
changing it, saving, or cancelling.

For the next feature build, replace step 4 with one specific feature check.
Keep the clipping, page-turn, and sleep/resume checks so the loop stays short.
