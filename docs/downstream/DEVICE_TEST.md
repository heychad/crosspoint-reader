# First X4 Pro feedback build

The immediate goal is a reviewed firmware candidate that Chad can test on his
Xteink X4 Pro, followed by small changes informed by that test. The larger
CrossInk integration remains in the separate `crosspoint-reader-render-fallback`
worktree for later stages.

The `2.0.0-chad.3-test1` candidate starts from the archived `2.0.0-chad.3`
source patch. The distinct version identifies the later statistics fix. It adds
custom top and bottom reader bars, side-button shortcuts and reader touch
gestures. It also contains the earlier reading-statistics implementation.
The later journaled book-move implementation, Google Drive wireless publication
and personal update channel are not part of this candidate. The inherited
finished-book move remains available and should be disabled for this test.

The follow-up `2.0.0-chad.3-test2` moves the top custom bar up two pixels and
reduces the left and right margins of both custom bars to the board's viewable
insets plus one left pixel. The progress line still draws only the completed
portion; there is no unfilled track. Reserved reading space is unchanged, so
this change does not trigger repagination. Check on the X4 Pro that the top
line remains visible: the shift puts it one pixel above the stock top inset in
the screenshot's portrait orientation.

The archived source patch and X4 Pro firmware image match their recorded
SHA-256 hashes. Chad reported that test1 booted, an existing book resumed, and
page turns and touch worked. Chad subsequently installed test2 and reported
that the side margins looked good, while the top bar was still one or two pixels
too low. Chad later installed test58 and confirmed basic reading, resuming,
Quick Lock, library filters, compact rows, and saved items. More recent local
source changes have not been installed or tested on the reader.

Before a reviewed device update, back up and verify the entire hidden
`/.crosspoint` directory, including settings, progress, bookmarks and clippings.
Also preserve `/.crossink-stats-backup` if present and keep the original books.
The official CrossPoint 2.0.0 X4 Pro image is saved
under `.cache/device-test/rollback/`; its SHA-256 matches the GitHub release.
Do not install until the candidate and backup are reviewed.

The stock Wi-Fi file server blocks downloads of hidden filenames and the Wi-Fi,
OPDS and KOReader credential files; its directory listing also does not report
every possible read failure. A wireless backup can verify the files retrieved,
but cannot by itself prove a complete copy of `/.crosspoint`. The X4 Pro offers
**File Transfer > USB Drive** to expose the SD card over a USB cable without
removing it; verify that it mounts and copy the state from there if available.

For a Wi-Fi handoff, preview the state backup first so the transfer size is
known. If hidden files were temporarily enabled, record the previous setting
and restore it afterward. Once the backup and its limitations are reviewed,
upload the application image to the card root under a fresh, unique `.bin`
name without requesting overwrite. Keep other file transfers idle: the stock
server's existence check and file creation are not atomic. Read the uploaded
file back and compare its size and SHA-256 with the packaged image before
selecting it. A failed or
interrupted upload may leave a partial file; do not select it or overwrite an
existing file. The packaged test `.bin` is the application image for the SD
updater; do not use the larger factory image for this step.

Verify that moving finished books to `/read` is disabled. Avoid clearing caches,
renaming or replacing books, downloading to existing paths, and installing
upstream OTA updates during this bounded test. Use a disposable EPUB for
experimenting; then check a known in-progress book's resume position, page turns,
configured bars and shortcuts, touch, light and sleep/wake. Read a protected/plugin book if available, without
replacing it. Record any regressions before expanding the feature set.

If global statistics are recovered from a backup, this candidate keeps those
statistics read-only until the files can be inspected. Reading remains available,
but neither per-book nor global tracking runs in that state, and the statistics
screen shows data as unavailable.

If the test firmware starts but needs to be rolled back, put the verified stock
X4 Pro `.bin` on the SD card, then use **Settings > System > SD Card Firmware
Update** and select that file. Keep the reader powered through the restart.
If it cannot start that menu, the official USB flasher may be needed; this
wireless test plan does not establish a recovery path for a nonbooting device.
Before restoring saved files, preserve any new reading progress made during the
test. Restore the verified `.crosspoint` backup only with the reader shut down
and the card accessed separately, then restart and check a known book's place.
