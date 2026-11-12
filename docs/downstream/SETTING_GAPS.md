# Candidate setting-key gaps

Source: CrossInk v1.6.1 (`9914146`) compared with unmodified CrossPoint 2.0.0 (`42d58da`).

A missing key is a discovery lead, not proof of a missing feature: renamed settings, different defaults, and equivalent controls need manual comparison. This list is preserved so small features are not lost in the larger UI groups.

| CrossInk key absent from CrossPoint SettingsList | Verification |
| --- | --- |
| `autoBackupStats` | Separate preferences import maps this toggle when experimental statistics backup is enabled. |
| `customBootscreenEnabled` | Pending semantic comparison |
| `dateFormat` | Pending semantic comparison |
| `dateSeparator` | Pending semantic comparison |
| `deviceName` | Pending semantic comparison |
| `disableReaderTouchscreen` | Separate controls import maps effective reader-touch state; menu touch differs. |
| `fileBrowserDisplay` | Separate library import maps the compatible one-line/two-line values. |
| `forceParagraphIndents` | Pending semantic comparison |
| `frontButtonOrientationAware` | Pending semantic comparison |
| `frontlightScheduleEnabled` | Pending semantic comparison |
| `frontlightScheduleEnd` | Pending semantic comparison |
| `frontlightScheduleStart` | Pending semantic comparison |
| `guideReadingEnabled` | Pending semantic comparison |
| `hideClock` | Pending semantic comparison |
| `hideFileExtension` | Separate library import maps the compatible toggle. |
| `homeButtonDoubleTapAction` | Pending semantic comparison |
| `homeButtonInReaderEnabled` | Controls import maps the enable state while protecting an existing Home Quick Actions owner. |
| `homeButtonLongPressAction` | Pending semantic comparison |
| `homeButtonTapAction` | Pending semantic comparison |
| `indexingMethod` | Global preferences and per-book imports preserve Incremental or Full chapter. |
| `leftEdgeDown` | Embedded and separate-file controls imports map compatible values; device checks remain. |
| `leftEdgeUp` | Embedded and separate-file controls imports map compatible values; device checks remain. |
| `lineHeightPercent` | Preferences import maps exact matching line-height presets. Other values are skipped. |
| `longPressBackAction` | Pending semantic comparison |
| `longPressMenuAction` | Pending semantic comparison |
| `longPwrBtn` | Pending semantic comparison |
| `nearbyReceiveFolder` | Pending semantic comparison |
| `pinchFontResizeEnabled` | Implemented; separate-file controls import stages compatible values; device checks remain. |
| `powerChordAction` | Pending semantic comparison |
| `publisherPageNumbers` | Pending semantic comparison |
| `readingIdleTimeThresholdUnits` | Separate preferences import maps valid 10-second units; device verification pending. |
| `rightEdgeDown` | Embedded and separate-file controls imports map compatible values; device checks remain. |
| `rightEdgeUp` | Embedded and separate-file controls imports map compatible values; device checks remain. |
| `screenMarginHorizontal` | Preferences import maps equal horizontal and vertical margins from 5 to 40 pixels. |
| `screenMarginVertical` | Preferences import maps equal horizontal and vertical margins from 5 to 40 pixels. |
| `sdFontSizeRange` | Pending semantic comparison |
| `shortPwrBtn` | Pending semantic comparison |
| `sideButtonChordAction` | Initial EPUB reader bindings implemented under reader-prefixed keys; full catalog/XTC parity pending |
| `sideButtonDownLong` | Initial EPUB reader bindings implemented under reader-prefixed keys; full catalog/XTC parity pending |
| `sideButtonDownShort` | Initial EPUB reader bindings implemented under reader-prefixed keys; full catalog/XTC parity pending |
| `sideButtonOrientationAware` | Pending semantic comparison |
| `sideButtonUpLong` | Initial EPUB reader bindings implemented under reader-prefixed keys; full catalog/XTC parity pending |
| `sideButtonUpShort` | Initial EPUB reader bindings implemented under reader-prefixed keys; full catalog/XTC parity pending |
| `stablePageNumbers` | Pending semantic comparison |
| `statusBarBookPercentageFormat` | Pending semantic comparison |
| `statusBarTimeLeft` | Pending semantic comparison |
| `swapLibraryFileBrowser` | Library import maps the effective order for CrossInk Minimal and Dashboard themes. |
| `tapToHideStatusBar` | Pending semantic comparison |
| `tiltPageTurnDirection` | Pending semantic comparison |
| `trackReadingStats` | Separate preferences import maps the master tracking toggle; device verification pending. |
| `twoFingerRotationEnabled` | Implemented; separate-file controls import stages compatible values; device checks remain. |
| `twoFingerSwipeDown` | Implemented in gesture checkpoint; device checks pending |
| `twoFingerSwipeLeft` | Implemented in gesture checkpoint; device checks pending |
| `twoFingerSwipeRight` | Implemented in gesture checkpoint; device checks pending |
| `twoFingerSwipeUp` | Implemented in gesture checkpoint; device checks pending |
| `uiScale` | Separate preferences import maps small and large scale values; device verification pending. |

## RC preference mappings added November 11

- `characterSpacing`: import only whole-pixel values representable by the native control; skip fractional values.
- `imageGrayscale`: combine with `imageRendering` to choose grayscale or black-and-white display. Placeholder and suppressed images retain their modes.
- `frontlightBrightness`, `frontlightWarmth`, and `frontlightOn`: import within the native value ranges.

These use the existing explicit import, snapshot, and recovery flow. The
historical list above is not a current feature-completion checklist.

The preferences import also preserves the compatible `fadingFix`,
`pwrBtnFootnoteBack`, and `frontlightRestoreOnWake` toggles. These and chapter
indexing use the same numeric values in the pinned CrossInk RC and this fork.
Missing values leave the current settings unchanged. Invalid values are ignored;
existing native values retain priority unless replacement is explicitly chosen.

Validation: the preference-import harness passed in default, statistics-backup,
and page-overlay configurations. Ten affected host checks passed in 2.23 seconds;
the default X4 Pro build passed in 68.31 seconds. Device behavior is unverified.

## Named font mappings added November 12

`sdFontFamilyName` now imports through global preferences and compatible owned
per-book fields. Valid names fit 31 UTF-8 bytes; built-in IDs and unsupported
names remain unmapped. Point size imports only alongside a compatible font.
Existing native choices and original CrossInk files retain their protections.
See [named font migration](BOOK_SETTINGS.md#named-font-migration) for fallback
behavior and resource limits. This does not add the deferred CrossInk font pack
or complete G3.

## Safe Mode mapping added November 12

The per-book Safe Mode flag now imports separately from the saved rendering
profile. The scope retains underlying embedded-style and Focus Reading values
while disabling them temporarily. A relevant layout edit or reset exits Safe
Mode; unrelated edits preserve it. [The migration notes](BOOK_SETTINGS.md#safe-mode-migration)
describe the optional native field and the remaining guide-setting gap.
