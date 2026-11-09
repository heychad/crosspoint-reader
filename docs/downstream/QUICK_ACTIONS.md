# Quick Actions integration (in progress)

This stage continues the full CrossInk port from `2.0.0-chad.3`. It is not ready
for installation. The previous reviewed source and binaries remain preserved.

The implementation adds five saved reader commands, configured under Settings
> Controls > Quick Actions. The menu and scrollable command chooser use the shared list UI,
and can also enter configuration from its final row. Empty slots are omitted.
A slot cannot open Quick Actions recursively. Slots are stored under the new
`readerQuickActionsV1` key; importing CrossInk's different numeric action schema
remains separate work.

Reader commands use one label table and the existing action dispatcher. New
entries include Home, files, library, reading statistics, screenshot, sync,
clipping, night mode, Focus Reading, touch, tilt and auto-turn cycling. Unsupported
commands are filtered for the active format/hardware. Existing numeric shortcut
values are unchanged; additions append to the enum.

In EPUB, the Reader Font Family command opens the existing family picker and
returns to the book. It can be assigned to a reader button or a Quick Actions
slot; unsupported formats do not offer it in Quick Actions. Cycle Font Family
switches directly among built-in and installed SD families whose names fit the
31-byte saved setting; CrossInk cycles built-in families only. Cycling is
currently available for EPUB.
If the font list is marked changed, the shortcut asks the reader to reopen the
book before cycling.

Quick Actions can be assigned to reader side/edge bindings, a Home-key gesture,
Power tap, Power hold, or Power + Up. Button-only readers also support Back and
Confirm hold. It has one
owning trigger. Assigning a new trigger clears the old override; Home-key fields
restore their normal default actions. These additions currently apply while a
reader is in front. The following control stage adds Power tap/hold, Power+Up, Back/Confirm hold and
Quick Lock; see [LOCK_CONTROLS.md](LOCK_CONTROLS.md). The remaining full CrossInk
command catalog still needs comparison.

Opening the menu stops auto-turn and clears queued manual turns. Selecting a command returns to the reader and waits for a fresh page before
execution. The wait uses a try-lock, permits Back to cancel, and resumes reading
statistics for commands that do not repaint. Commands such as screenshot reuse
the existing render-time capture path. No additional task, framebuffer, network
session or background polling is introduced. The shared UI host retains its existing allocations; shared translated slot labels and
bounded command arrays belong to one menu activity while its reader remains
on the stack. The picker scrolls instead of growing an option dialog beyond
the display. Rows are supplied on demand without allocating a full list.

Final verification passed: 1000 host tests, all-severity X4 Pro cppcheck, and
final X4 Pro/default C3 builds. All four independent initial reviews and their
one targeted follow-up are complete with no outstanding findings. The fixes
cover scrolling beyond the old 16-option popup limit and preserving one trigger
through complete web-setting batches.

X4 Pro uses 112,276 bytes static RAM and 5,480,026 bytes program flash. C3 uses
67,224 bytes static RAM and 5,628,267 bytes program flash. These figures do not
establish runtime heap/stack safety or physical behavior. Human design acceptance,
local commit permission and device validation remain pending. No firmware was
flashed or published.

## Device checks

- Configure all five slots and try run/configure/choose/back in every orientation.
  Scroll a side/edge binding to Quick Actions and to the final command, save it,
  then reopen the picker; both must remain selected and reachable.
- Reassign the menu between side, edge, Home, Power tap, Power hold and Power + Up.
  On button-only readers, also check Back and Confirm hold.
  Only the chosen trigger should open it; other command bindings must remain.
  A held trigger must not operate the newly opened list when released.
- Change bindings through the web UI, including a complete settings snapshot.
  The new trigger must take ownership immediately and survive restart.
- Choose screenshot: it must contain the book, not the menu. Check one command
  executes after return, cancellation with Back, and no extra queued page turn.
- Verify reading time excludes the menu and resumes after light-only commands;
  test statistics, sync, clippings, text changes and auto-turn cycling separately.
- Repeatedly open/configure/close while measuring internal heap and largest block.
  Test XTC format/hardware filtering and retain normal Power/sleep behavior.


## Resource and lifecycle notes

The new menu retains its reader on the existing activity stack. Its added data
is five visible action values, one bounded action-index array and scalar
mode/selection fields. Slot labels use shared translation entries. It reuses the shared UI host and supplies
only requested rows. It has no framebuffer, worker or active network resource.
Navigation commands hand off to existing activities and their existing cleanup.
The reader adds one pending action and one paint-generation marker. JSON settings
serialization retains its existing dynamic document allocation. Runtime heap,
largest block and input responsiveness still require physical checks.
