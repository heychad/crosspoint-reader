# Quick Lock and physical controls

This is the `2.0.0-chad.5` control stage, based on the preserved `chad.4`
Quick Actions source. Host/build verification is complete; human acceptance and physical testing
remain pending. No device has been flashed with this version.

## Behavior

Settings > Controls adds Power tap, Power hold (400 ms), Power + Up, and
Back hold (1 second) / Confirm hold (400 ms) while reading on button-only devices. Default
preserves existing controls. These fields use the shared command catalog;
commands specific to EPUB or a reader do nothing outside that context. Outside
a reader, Home, files, library, refresh, frontlight, Sleep and Quick Lock are
supported. Reader-only front-button remapping and remaining catalog differences
are separate parity work.

Quick Lock is available on those controls, EPUB side-button taps/holds/chord,
and Home-key tap/double-tap/hold. It is intentionally absent from touch edge
and Quick Actions slot pickers: unlocking requires an identifiable physical
origin. The same gesture unlocks. All held buttons must first be released;
a long action cannot also run its short release action. Power + Down retains
its screenshot function and consumes both buttons until released.

While locked, normal activity input/rendering and Bluetooth navigation stop.
The frontlight switches off without changing its saved setting. Unlock restores
its previous on/off state and the current screen. Reader statistics and plugin
reading sessions pause; auto page-turn stops. Pending repaint requests trigger
a fresh paint after unlock. The lock is temporary and clears on restart.
A render in progress, busy transfer/OTA activities and exclusive USB storage
cannot be locked; retry after the page or operation finishes.
The configured inactivity timeout still sleeps the locked reader, measured
from lock entry, even when a button remains held. The Never setting disables
this timeout. Badge pixels are restored before retaining a quick-resume or
transparent sleep frame.

An explicit Power tap replaces legacy short-Power/double-click behavior.
With a custom tap and Default hold, holding Power still sleeps after 400 ms.
With Default tap and a custom hold, the old short-tap action remains; its Sleep
case waits for release so it can be distinguished from the custom hold.
Quick Actions still has one trigger across all old and new binding fields.

## Ownership and resources

The main loop owns lock/chord/hold state and a small pending control request.
No additional task or polling loop is created. ActivityManager serializes the
lock transition and the existing renderer with RenderLock. It holds one
240-byte region buffer for a 40 by 40 lock badge; this is not a second screen
buffer. No lock state is persisted and no book/cache path is changed. New
bindings use existing settings JSON serialization and validation. The pinned
FreeInk SDK and existing protected-content routes remain unchanged.

Final checks: 1042/1042 host tests and all-severity X4 Pro cppcheck passed.
X4 Pro uses 112,708 bytes static RAM and 5,485,870 bytes program flash;
C3 uses 67,656 and 5,635,101 bytes respectively. Both final builds passed.
C3 retains the existing wolfSSL macro-redefinition warning, also present in
chad.4; build-warning cleanup remains before any approved firmware commit.

Four independent initial reviews and one targeted follow-up were completed.
The final prescribed correction removed the aggregate SDK duration from front
contacts and disabled their legacy reader hold handlers. Overlapping-button
lock/unlock and short Back release tests verify that correction; it was not
sent through a third review round. No verified finding remains unaddressed.
Runtime internal heap, largest block, stack and physical display behavior still
require device measurement. The human maintains the additional input routing,
settings fields, pause hooks, and small badge restore path in this fork.

## Physical checks before installation acceptance

- Test Power tap/hold, Power + Up, each side binding/chord, and Home
  tap/double-tap/hold. Lock and unlock only with the assigned gesture, with no
  page turn, menu selection, screenshot, light toggle or sleep from its release.
- Test a different gesture while locked: it must do nothing. Keep the original
  trigger held beyond the sleep timeout; the reader must still sleep.
- With custom Power hold and Default tap, test legacy short tap, double-click
  frontlight, long hold and Sleep. With custom tap, double-click must run only
  that tap action; Default hold must still sleep.
- Test Power + Down with both release orders and all three buttons pressed.
  Exactly one screenshot, no page turn or sleep, and no Power + Up action.
- Test all four orientations and an open reader overlay. The badge must restore
  cleanly, frontlight state must return, and reading time must exclude the lock.
  Repeat with the frontlight initially off and night mode enabled.
- Test Bluetooth while locked, pending redraws, Home/Library/settings contexts,
  Back/Confirm remapping on C3 and idle-timeout sleep/resume in a partly read EPUB.
- Attempt lock during active transfer/update: it must leave that activity running.
  Confirm 777 EPUB paths/sizes and saved positions remain intact after a future
  reviewed install; retain existing backups and rollback firmware.
- Measure internal/PSRAM heaps and largest blocks separately after repeated
  lock/unlock and book/menu/Wi-Fi transitions. No leak; C3 must retain >50 KB free.
