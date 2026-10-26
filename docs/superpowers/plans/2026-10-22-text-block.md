# TextBlock Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for the module and integration, with independent review after verification. Track steps with the checkboxes below.

**Goal:** Build a reusable read-only paragraph bubble as the static QML module `TextBlock`.

**Architecture:** One public `TextBlock` QML component owns paragraph sizing/rendering and an optional caller-defined background slot. Callers own events, focus, clipboard actions, menus and feedback. Private C++ content/layout classes and a `QQuickItem` use `QTextLayout` and `QSGTextNode`; the demo supplies atomic content snapshots and simulates streaming.

**Tech Stack:** Qt 6.12 public APIs, C++23, QML, CMake 3.24+, Qt Test.

**Spec:** User-approved design in this chat; the requirements below are its durable record.

## Global Constraints

- One bubble instance contains one paragraph; the same module supports many instances.
- Text hugs content until `maximumWidth` (including padding), then wraps and grows vertically.
- `content` is `{ text, ranges: [{ start, end, bold, highlight }] }`; ranges use UTF-16 `[start,end)` offsets. Bold and highlight combine.
- Text and external styles update together. Invalid ranges are ignored; valid ranges must not split a grapheme cluster.
- The demo can copy the whole plain text through caller-owned actions. No editing, drag selection or user-authored formatting.
- The module owns text rendering and layout padding (default 0); it hosts an optional `background` component (default null) behind the text, filling its actual size. Background implicit geometry does not affect text or component implicit size. Callers define the background appearance and all interaction/feedback. The module has no Controls, clipboard, Chunfe business, logging or theme dependencies.
- Module directory/target: `modules/text_block` / `text_block`; QML URI/type: `TextBlock` / `TextBlock`.
- The experiment includes streaming, resizing, mixed scripts, emoji, style overlap, multiple instances, and a standard Text comparison.
- Work in the existing clean `dev` checkout for this local experiment. Leave changes reviewable and uncommitted; publishing is outside this task.

## Review Focus

- Font bearings, mixed scripts and emoji must not clip or get split by style ranges.
- Short/empty/zero-width content and changing maximum widths must keep finite stable dimensions.
- Style-only snapshots and streaming changes must not leave stale scene graph nodes.
- GUI layout and render-thread node ownership must remain separated, including scene recreation.
- A second consumer must be able to import the module without application-root paths or global settings.

## Task 1: Importable module and public behavior

Files: root/module CMake files; `modules/text_block/tests/text_block_tests.cpp`; `text_block_content.*`, `paragraph_layout.*`, `paragraph_item.*`, `TextBlock.qml`.

Current interface (revised by Q16–Q17): `content`, `font`, `maximumWidth`, `padding`, `background`, `textColor`, `highlightColor`; implicit dimensions and read-only layout diagnostics. The original `radius`, `backgroundColor`, `copy()` and `copied()` API is superseded by caller composition.

- [x] Run a public QML import test and observe failure before the module exists.
- [x] Implement atomic content validation, grapheme-safe style ranges, layout and scene graph rendering.
- [x] Implement the QML shell, sizing and clipboard interaction.
- [x] Verify import, wrapping/unwrapping, style overlap, streaming isolation, invalid ranges, empty content and clipboard via Qt Test.

## Task 2: Experiment page and independent consumer

Files: `Main.qml`, module README, `examples/text_block_consumer/`.

- [x] Add controllable examples for width, streaming, styles, mixed text and multiple bubbles.
- [x] Provide a standard Qt Quick Text comparison and layout diagnostics.
- [x] Build/run a second minimal consumer using only `add_subdirectory` and Qt.
- [x] Document API, commands and measurement limits.

## Task 3: Verification and review

- [x] Build Debug, run Qt tests, QML lint and the existing 43 Python release tests.
- [x] Run the app and inspect rendered output on the available macOS backend.
- [x] Obtain independent code review and resolve concrete findings.
- [x] Record observed results and remaining platform limits below.

## Execution record

### Main page warnings and editor component discovery

Follow-up request: fix the existing main-page style/layout warnings and unknown-component errors in Qt Creator. This supersedes the outstanding warning note in the boundary-revision record below.

- [x] Move the custom button into application-owned `AppActionButton.qml`, importing the customizable Basic style only there. Preserve button appearance and the native style of other page controls. Separating imports also avoids Qt 6.12's renamed-type diagnostics from mixing Basic and default Controls in one file.
- [x] Break the repeated paragraphs' layout feedback by measuring available width from the outer card, not the nested layout whose size depends on the paragraphs.
- [x] Supply the generated module root through Qt Creator's cached `QML_IMPORT_PATH`, preserve existing paths, and add explicit self-imports in Main.qml and TextBlock.qml for the classic code model's C++ type discovery.
- [x] Track the `.qmlls.build.ini.part` dependency omitted by Qt 6.12's metadata build rule so incremental reconfiguration refreshes editor paths and resources.
- [x] Add regression coverage for strict metadata-driven QML lint and the real main page's resizing, 6/30/100 instances, width changes and streaming completion, failing on runtime warnings.

Verification: all six Debug CTest suites and all 43 Python release tests pass. Application/module lint targets pass; metadata-driven lint additionally enforces zero warnings for Main.qml, AppActionButton.qml and TextBlock.qml. Main-page tests also pass on the native macOS window backend. Qt Creator's active RelWithDebInfo configuration was rebuilt, and screenshots confirm that ParagraphItem and TextClipboard M300 red underlines disappear. The newly launched application log contains only the normal QML debugging notice; no style, recursive-layout or missing-component warnings occur. The independent consumer builds, lints and renders after the module import change. Independent review found no actionable issues. Other operating systems remain unverified.

### Display-only boundary revision (Q16–Q17)

The user explicitly moved events, copy operations and ToolTip outside the module, then confirmed an optional background slot and padding default 0. This supersedes the original interaction ownership recorded below; the earlier completed tasks are historical verification of the initial version.

- [x] Replace the built-in background with a caller-provided Component slot and remove QML/C++ interaction code and Controls dependency.
- [x] Migrate the experiment and independent consumer to explicit backgrounds and caller-owned copy interactions.
- [x] Verify default transparency, padding, background lifecycle/geometry and caller event handling; retain text/font/performance regressions.
- [x] Build and lint callers/module, run tests and independently review the new boundary.

Verification: Debug application build and app/module QML lint pass; all five CTest suites pass (including new caller copy interaction coverage). Both independent consumer configurations (bundled fonts and system fonts) build, lint and render successfully. Native macOS screenshot inspection confirms caller-provided backgrounds, text and highlight rendering; the copy interaction tests also pass with the native window/clipboard backend. All 43 existing Python release tests pass. Independent standards and specification reviews found no actionable issues. The system-font offscreen example emits Qt's existing missing `Sans Serif` alias warning; bundled-font and native checks are clean. Other operating systems remain unverified.

The main application also starts successfully in an offscreen smoke check. It still emits native-style customization warnings for the existing ActionButton and a recursive rearrange warning in the multiple-paragraph ColumnLayout. Running the earlier, unchanged `Qt_6_12_0_for_macOS_RelWithDebInfo` binary with the same offscreen/software configuration reproduces both warnings, confirming they predate this boundary revision. They remain outside this change.

### Earlier implementation and appearance work

- Baseline: working tree clean on `dev`; 43 Python tests passed; Qt 6.12.0 macOS kit found.
- User's `继续` follows the approved design and naming; implementation proceeds without reopening those choices.
- Initial implementation: Debug build, application/module QML lint and both CTest executables passed; 43 existing Python tests passed. The standalone consumer builds, imports and renders without Chunfe dependencies.
- Native macOS output was inspected. User reports inferior static text appearance, background and border relative to Qt Quick Text; visual acceptance remains open. Current comparison uses different backgrounds and a click-focus border only on TextBlock, so presentation must be aligned before judging the rendering route.
- User also requests Quick Text layout counts. Qt has no public aggregate layout-count property. `lineLaidOut` can observe activity but connecting it changes some layout optimization paths; observation counts must not be presented as an undisturbed performance comparison.
- Independent specification review found no actionable mismatch. Code quality review found an italic wrapping case where stable insets still permit glyph ink outside the measured width. The fix now passes its regression and an independent 2,016-case scan (zero failures, previously 15).
- Latest Debug build, both QML lint targets and both CTest executables pass after the wrapping fix. Windows and Linux rendering have not been exercised locally.
- Q11: prioritize static glyph appearance, background and border. Q12: user confirms both relayout observation and performance comparison, with separate statistics.
- A controlled cocoa/Metal/DPR=2 probe confirms the glyph appearance difference even with identical white backgrounds, integer origins, font and glyph positions. Disabling clipping, setting the item antialiasing flag, using the basic render loop, and rebuilding the layout on the render thread do not remove it. All experimental variants are isolated under `/tmp`.
- Confirmed rendering cause on this setup: Qt 6.12's `QSGDistanceFieldGlyphNode::recycle()` resets its antialiasing mode to gray; newly created glyph nodes receive the context's default subpixel mode. Quick Text recycles nodes while the public TextBlock path clears them. Running the controlled probe with `QSG_DISTANCEFIELD_ANTIALIASING=gray` reduced both text ROI differences to zero pixels (previously 36,584 and 21,115 differing pixels), with no font/layout changes. The app and standalone consumer now default to gray before creating QGuiApplication, preserving explicit environment overrides. The module does not modify process state.
- Static comparison now shares background, radius, padding and font. Mouse focus no longer adds a border; keyboard focus retains its indicator. A regression test covers keyboard focus followed by pointer focus.
- Observation mode reports TextBlock rebuild calls, Quick Text first-line `lineLaidOut` callbacks and shared input updates separately. The native window confirmed both counters update during width changes and streaming (71 rebuilds / 72 first-line callbacks for one observed sequence of 72 input changes; coalescing and different counting scopes are explicitly documented).
- Performance mode disconnects the native layout observer and runs each renderer separately on the same generated snapshots and widths: 5 warmups, 30 measured batches, median/P95 for GUI update + polish, update-to-frame-submission and whole-window synchronization-to-render-recording wall time. The helper rejects stale frame callbacks and cancelled generations. Regression tests cover ordinary cancellation, reentrant cancellation and restart; independent review found no remaining actionable issue in this single-window path.
- Final Debug build and both QML lint targets passed without warnings. All three CTest executables passed (experiment metrics, module integration, layout/content); all 43 Python release tests passed. The independent consumer was rebuilt successfully with the application-level antialiasing policy.
- Native 6-instance measurement completed with 30/30 valid samples per renderer. Example median/P95: GUI update + polish 1575.9/4479.0 µs vs 3727.0/6105.4 µs; update-to-submission 16.243/17.527 ms vs 15.869/17.660 ms; frame preparation 1.072/2.344 ms vs 1.375/2.090 ms (TextBlock then Quick Text). This Debug, overlapping-instance experiment demonstrates the measurement workflow; it does not establish production speed, FPS or GPU performance.
- Native stop/restart was exercised at 100 instances; the restarted run reached the completed state with 30/30 valid samples per renderer. The independent consumer also rendered successfully to PNG using its offscreen/software smoke-check path.
- Local visual checks use macOS cocoa / Metal / DPR=2. Windows, Linux, other display scales and other font sets remain unverified. Screenshots and isolated investigation artifacts are kept under `/tmp`, outside the reusable module.
