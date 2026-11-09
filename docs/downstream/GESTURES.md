# Reader gestures: implementation checkpoint

This stage builds on the reviewed combined `2.0.0-chad.2` source without changing
its saved statistics or book-cache identities. Candidate version: `2.0.0-chad.3`.
It implements CrossInk's pinch sizing, two-finger rotation and directional
bindings, continuous edge light adjustment, plus warmth/text/half-turn shortcuts.
It does not complete the full CrossInk feature-parity goal.

## Ownership and compatibility

The pinned FreeInk SDK owns hardware polling and completed multi-touch events.
`HalGPIO` exposes calibrated normalized contacts, including a single-touch
fallback on other controllers. `MappedInputManager` converts coordinates through
the current renderer orientation. No extra polling, task, framebuffer or SDK
revision is added.

A reader-owned controller consumes each recognized gesture through release.
It starts blocked until inherited contacts end, and rejects extra/invalid
contacts and identity changes. Physical Home retains priority. Menus and end
screens disable recognition. The manager still processes pending transitions
and repaint requests when a gesture owns the frame. Eligibility and coordinate
sampling use a nonblocking render lock. During a repaint, unclaimed contacts stay
with normal input; an already-owned drag is canceled without blocking polling.

Touch-only reader state is bounded: the recognizer is limited to 256 bytes, with
one reusable input frame and a small light transaction. C3 builds omit that
reader state. Contact handling does not allocate. Font steps scan the installed
sizes without constructing a vector; text changes reuse the existing visible
position preservation and re-pagination path. New settings add six bytes before
structure alignment. UI enumeration and settings saves keep their existing
allocation behavior. Runtime heap/stack measurements still require hardware.

Light drags change hardware and in-memory settings as the finger moves. They
write settings once at completion. Rotation/pinch within an existing two-finger
light drag, replacement contacts, or render-lock contention roll back the light
change. Adding a second finger to a one-finger edge drag instead finishes that
adjustment; it does not start another gesture. Leaving the reader or entering an
overlay finishes a current adjustment. A power interruption during the drag can lose
the unsaved adjustment. No continuous value overlay is added in this stage.

Persisted side/edge action numbers 0–15 remain unchanged. New actions are
appended. The four two-finger keys use CrossInk's original 0–8 values. Duplicate
actions normalize to one direction, with the most recently edited direction
winning. Runtime probes hide multi-touch options on unsupported controllers
without dropping their saved values. The later embedded-key migration imports
legacy edge values from the current settings document only when each modern key
is absent. It does not read the separate CrossInk settings file. Existing fork
side/edge settings remain supported.

## Checks and review

Final checks passed: 989 host tests, X4 Pro cppcheck at all severities, and final
X4 Pro/default C3 builds. X4 Pro uses 112,268 bytes static RAM and 5,468,682 bytes
program flash. C3 uses 67,224 bytes static RAM and 5,616,935 bytes program flash.
Both remain within their 6,553,600-byte application slots. Runtime memory and
physical behavior are unverified.

All four initial independent reviews and one targeted follow-up round were
collected. Findings covered menu ownership, render blocking, replacement
contacts, the initial inherited-contact guard and stale documentation. The
last inherited-contact correction follows the reviewers' explicit prescription;
main-agent regression checks cover it. No third review round was used.

The X4 Pro release ELF reserves a 160-byte function frame for reader gesture
input, 80 bytes for controller update, and 48 bytes for light application. These
are individual compiler frames, not complete call-chain or runtime stack usage.

Human design acceptance/local-commit permission and all device checks remain
pending. No firmware has been flashed. The updater still targets upstream
CrossPoint; the personal release channel remains separate unfinished work.

## Device verification

Use a disposable EPUB after backing up reading state. Test portrait, inverted,
landscape clockwise and landscape counterclockwise:

1. Pinch/spread: one adjacent installed font size, stable reading position, no
   additional page turn on lift. Repeat at both size limits and with a sparse
   installed font family.
2. Rotate two fingers: one quarter-turn in the opposite direction. Disable the
   setting and verify there is no rotation. Verify ordinary page turns still work.
3. Bind every two-finger direction. Assign the same action elsewhere and verify
   its old direction clears. Lift fingers separately; no extra tap should fire.
4. Bind left/right edge brightness and warmth, drag to a limit then back, and
   leave the band. Check one save on finish and unchanged light on unsupported
   hardware. Add a second finger to an edge drag: the adjustment finishes. In a
   two-finger light drag, rotate/pinch: the old light value returns.
5. Hold a contact while opening/closing a menu, press Home mid-drag, visit the
   end screen, sleep and reopen. No contact should operate two screens.
6. Disable touch reader controls; test ordinary side-button controls. Verify XTC
   light gestures, and that XTC text/rotation bindings have no effect.
7. Measure internal/PSRAM free and largest blocks, stack high-water marks and
   save latency across repeated gestures/menu/font transitions. Build RAM totals
   do not establish runtime safety.
