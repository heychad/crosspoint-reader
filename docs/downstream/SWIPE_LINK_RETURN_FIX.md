# Reader swipe and link return

November 14, 2026. Chad reports that left-to-right swipes return to Contents,
while left taps turn one page back. The host regression reproduces a matching
route when the reader has saved link-return history. Device confirmation is pending.

## Cause and correction

The shared input mapper also exposes a left-edge rightward swipe as logical
Back for menus. The reader's normal Back handler already ignores that gesture
so page turning can use it. EPUB's earlier link-return handler did not: with a
saved origin, it restored that origin before reaching page turning. A Contents
link can supply such an origin, and link history survives reopening the book.

The link-return handler now uses the same gesture exclusion as normal reader
Back navigation. Physical and remote Back still return through link history;
swipes retain their configured page direction. Menus retain swipe-to-Back.
Disabled page gestures do not turn or consume a saved origin. No persistent
format, setting, SDK dependency, buffer, or task changes.

## Checks

`test/test_reader_link_back_swipe.py` compiles the production mapped swipe,
Back-release, link-return, and shared reader handlers against the pinned SDK's
edge geometry. It fails on test59 when an edge swipe consumes a saved origin.
The correction passes in all four orientations, both book reading directions,
edge/middle swipes, disabled gestures, tap controls, physical/remote Back, and
long Back. AddressSanitizer and UndefinedBehaviorSanitizer pass together.
Existing page-gesture, Back-override, and reader-gesture routing checks pass.

The intended patch version is `2.0.0-chad.3-test60-swipefix-x4pro`, built with
the same `x4pro-chad-test` configuration as test59. All 2,452 cumulative host
checks pass in 177.23 seconds. The X4 Pro build passes in 193.06 seconds, using
113,100 bytes static RAM and 5,805,982 bytes flash. A combined correctness,
architecture, input, and resource review found no actionable findings in source
`1325b565a078ac9ec71b228e1ecab927f77c2a8b`. Overlays own input before this branch,
and header targets clear on activity transitions. The native harness models the
renderer and extracts production handlers; device confirmation remains necessary.

The verified local package is `.cache/device-test/test60-swipefix-1325b565/`.
Its `SWIPE60.BIN` is 5,811,248 bytes, SHA-256
`10ef1ab1098e178d377561cf9b03ac570d68c2e84d650b95dd57c4623bc4e38e`.
The ESP32-S3 header, image checksum, validation hash, and reader version match.
All six package checksums pass. Non-version flags, the SDK pin, and all three
patched SDK source hashes match test59; all 22 library links resolve here.
Fresh backup, transfer/readback under `/zDev/`, installation, and device checks
remain pending. Test59's package is unchanged.

The November 14 release-channel check still finds CrossPoint 2.0.0 stable and
CrossInk v2.0.0-rc2. The patch keeps the existing release references and SDK pin.

## Short device check

After a fresh state backup and reviewed installation, reopen the affected book
at the saved passage. Swipe left-to-right once from the left edge, then from
the middle. Each should turn one page back without returning to a linked page.
Check a left tap and a forward swipe, then sleep/wake and verify the passage.
Use another orientation you normally read in. If needed, follow a Contents link
in a disposable book first so there is a known saved return origin.
