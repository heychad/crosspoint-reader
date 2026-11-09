# First feature-group validation

This records the isolated first group in the full CrossInk parity project.
The combined candidate supersedes its binaries and includes the later end-screen
shortcut correction; see INTEGRATION.md. It is not a claim of full parity or completed device testing.

## Behavior and ownership

- The custom bar switch defaults off. Existing CrossPoint bar fields are kept;
  the versioned custom object initializes from those settings.
- Rendering, link/word selection, clipping hit-testing and the section viewport
  use the same computed page bounds. Reflow uses the existing visible-text
  offset and cache validation; no progress path/hash migration is introduced.
- The shared mapped-input layer consumes handled side-button contacts through
  their release, across toolbar or child-activity transitions. Ordinary touch
  and remote clicks remain independently routed.
- Existing side layout/direction policy has one resolver. Default hold rotation
  retains its previous direction.
- Numeric-only bars do not fetch chapter metadata. A requested chapter title is
  cached per spine in a bounded 192-byte reader-owned field; the existing book
  title is borrowed. Rendering uses fixed scratch and introduces no task or
  second framebuffer.

## Recorded checks

- Clean CrossPoint 2.0.0 X4 Pro baseline build: passed (111,764 bytes static RAM,
  5,415,026 bytes program flash).
- Initial feature X4 Pro build: passed (112,260 static RAM, 5,440,066 flash).
  This preceded review corrections and the personal version label.
- Initial serial host suite: 897/897 passed, including seven new tests.
- Parallel parser cases fail on the untouched baseline because they share one
  temporary file. Serial execution is used for the whole-suite checkpoint.
- Twelve focused personalization tests pass after the corrections. Additional
  production-method harnesses cover mapped side-contact suppression and
  clipping hit-testing with a top inset.
- Final serial host suite: **902/902 passed** (19.04 seconds).
- Final X4 Pro cppcheck: passed with low, medium and high severity gates.
- Final X4 Pro build: passed, 112,260 static RAM / 5,440,750 program flash bytes.
  Delta from clean baseline: +496 RAM / +25,724 flash bytes.
- Final default C3 build: passed, 67,208 static RAM / 5,594,465 flash bytes.
- Build tools: pioarduino 6.1.19. A conflicting newer nested installation was
  corrected before the final successful builds; no SDK or source pin changed.

Full logs are retained in `.cache/fork-logs/` in the working checkout; the clean
baseline logs are in the baseline checkout's directory of the same name.
Runtime free heap, stack high-water marks and physical reading remain unmeasured.

## Independent review

The initial four reviews covered correctness, architecture, embedded resources,
and English/docs. Findings involved contact ownership, selection coordinates,
legacy auto-turn text, default rotation direction, metadata lookups, and docs.
The one permitted targeted follow-up confirmed those corrections. It found
two further issues: a single-side hold could suppress the other held contact,
and a BLE notice could use an empty bottom bar instead of a configured top
title. Both were corrected and covered by new regression cases, the full host
suite, cppcheck and both final builds. Those contained fixes received main-agent
verification after follow-up, not a third independent round. No known finding
remains open; hardware behavior and human acceptance remain unverified.

Frozen independent review patches are retained in `.cache/fork-logs/`:
initial SHA-256 `323d6d87c0ffa6eff04222017a72e5286372d1233ebd0020cf0e94bca1ac194f`;
follow-up SHA-256 `45e3e1a343b7a47d3d33ed1fb4f36739f44f38231a991efb14046191aa642a6d`.
A review is evidence for the maintainer, not automatic approval.

## Device check after the reviewed build is accepted

1. Keep the verified backup and known-working firmware. Confirm existing EPUB
   paths and progress before applying a custom build.
2. With custom bars off, open an in-progress book and verify the old layout and
   resume position. Hide all legacy footer items, enable automatic page turn,
   and check that its message is visible.
3. Enable top/bottom layouts, including a top title taller than the margin.
   Check page turns, clipping selection/removal, dictionary lookup, footnotes,
   and reopen the book. Repeat portrait, inverted, and both landscapes.
4. Assign each side hold and the chord to Reader Menu. Hold to open, continue
   holding, then release: no menu movement or chapter jump should occur. The
   next fresh press should work in both list and toolbar menu styles. Stagger
   the two side holds and confirm the second contact is still recognized.
5. Mix a custom tap with Default hold rotation; both side directions should
   preserve the stock mapping. Exercise disabled actions and rapid presses.
6. Try edge brightness shortcuts, the normal control center, Home gestures,
   sleep/wake and frontlight restore. Check saved brightness and progress.
7. Repeated book/menu/lookup transitions require measurements of internal and
   PSRAM free heap and largest blocks. Host tests and static size reports do
   not establish runtime memory stability.
