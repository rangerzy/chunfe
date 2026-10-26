# Liquid calendar selection implementation plan

> **For agentic workers:** Use superpowers:executing-plans or superpowers:subagent-driven-development to execute the scoped tasks below.

**Goal:** Move the selected blue date background as a directional liquid drop.

**Architecture:** A reusable Qt Quick Shape draws one shared indicator beneath the date labels. Calendar selection remains immediate; visual motion and delayed adjacent-month navigation are separate state.

**Tech stack:** Qt Quick 6.12, Qt Quick Shapes, Qt Quick Test.

**Spec:** User-approved chat decisions, recorded below.

## Approved behavior

- Preserve the compact 492 × 532 calendar, 28px date labels and 50px settled indicator.
- Flow from the previous selected cell toward the clicked cell by screen coordinates, including diagonals.
- Stretch toward the destination, develop a short rounded tail, then gather into a circle; default duration 240ms and configurable stretch.
- Limit tail extension to 25px for the 50px indicator. Retargeting must not add old and new deformation together.
- On reversal, collapse the old tail for approximately 40ms while retaining its heading, then adopt the new heading. Further clicks update the destination without restarting collapse.
- Derive label coverage from the current geometry synchronously, rather than the renderer's previously polished path.
- Rapid clicks redirect from the current liquid position; do not queue or restart on repeated clicks of the same target.
- Clicks immediately update the selected date and emit/log it, including repeated valid clicks.
- Animate to a visible adjacent-month date before switching months.
- When the previous selection is not visible, gather locally at the new target.
- Manual month navigation cancels motion and restores the selection if visible.
- Keep labels stationary and adapt their contrast to the moving blue background.
- Preserve the user's minimum month configuration (Main.qml: September 2026).

## Review focus

- Rapid reversals must not teleport or execute an obsolete pending month switch.
- Adjacent-month selection followed by manual navigation must respect the latest action.
- A hidden previous selection must not invent an offscreen flight.
- Resizing and external date/month changes must not leave stale indicator coordinates.
- Both software and GPU rendering must show a coherent liquid silhouette and readable text.

## Task 1: Liquid indicator

Files: `LiquidSelection.qml`, `tests/tst_liquid_selection.qml`.

Interface: `duration`, `stretch`, `diameter`, `color`; `running`, `centerX`, `centerY`, `targetX`, `targetY`, `deformation`, `progress`, reactive `motionRevision`; `snapTo(point)`, `flowTo(point)`, `gatherAt(point)`, `clear()`, `covers(point)`; `settled()` only on natural completion.

- [x] Add and run tests for directional flow, retargeting, final circle, gathering and cancellation.
- [x] Implement a bounded Bézier droplet and interruptible motion with Qt Quick Shapes.
- [x] Run the indicator tests and QML lint.

## Task 2: Calendar integration

Files: `PastDateCalendar.qml`, `CMakeLists.txt`, `tests/tst_calendar.qml`.

- [x] Add failing integration checks for immediate selection with delayed visual arrival, rapid retargeting, delayed adjacent-month navigation, manual cancellation and hidden-source gathering.
- [x] Map date delegates into the shared indicator coordinate space; replace per-cell selected fill.
- [x] Keep pending automatic month navigation separate from immediate selectedDate and dateSelected.
- [x] Sync visibility/position on manual navigation, external selection changes and resizing.
- [x] Register the component in the QML module and run the complete interaction suite.

## Task 3: Verification

- [x] Build with CMake and run qmllint and CTest.
- [x] Capture actual start/middle/end frames and inspect long, diagonal and interrupted motion.
- [x] Verify the native GPU renderer if available; retain a preview artifact.
- [x] Request independent read-only review and fix material findings.

## Verification evidence

- CMake build and qmllint completed successfully.
- CTest: 49 QtTest results passed, 0 failures (45 test cases plus setup/cleanup).
- Inspected software-rendered and native macOS Metal (Apple M2) start/middle/end frames.
- Independent review finding about reentrant selection/navigation was reproduced, fixed, and regression-tested.
- Compiled application startup produced no runtime diagnostics.

## Follow-up: rapid-reversal fix

- [x] Reproduce the user's horizontal reversal and delayed label-contrast failures.
- [x] Shorten movement and tail; collapse before turning and keep deformation bounded.
- [x] Use one geometric outline for rendering and synchronous coverage.
- [x] Check quick repeated clicks, short-distance motion, cancellation and existing calendar behavior.
- [x] Record the corrected behavior with macOS Metal and rerun lint/build/tests.

Rapid-reversal verification: immediate label-contrast and off-row-tail regressions failed before the fix and now pass. Native Metal captures cover repeated neighboring-date clicks plus a diagonal move. Independent review found no remaining actionable issue. The short-tail peak test samples animation change events to avoid missing the brief peak between polling intervals.
