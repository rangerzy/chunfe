# Chunfe Project Structure Implementation Plan

> **For agentic workers:** Execute the approved tasks below and verify each project independently. Steps use checkboxes for tracking.

**Goal:** Open the root project to run Chunfe; open each example separately to run it.

**Architecture:** `src/app` owns startup, `src/core` owns application business logic, and `src/gui` owns presentation (QML and any future C++ adapters). Reusable modules remain under `modules`. Examples have standalone CMake entry points and never enter the root build graph.

**Tech Stack:** Qt 6.12+, C++23, CMake 3.24+, Ninja.

**Spec:** User-approved discussion in this chat: all three recommendations accepted, direct implementation requested, and CI must not manage examples.

## Global Constraints

- Examples may reference repository modules/resources and override dependency paths.
- The root application starts a minimal window with bundled fonts; no experiment UI moves into it.
- Core stays free of UI dependencies; do not invent business classes before they are needed.
- Preserve existing staged/uncommitted user changes; no commit, push, or release is part of this task.
- CI handles only the formal application, with its new entry path.

## Review Focus

- Root target lists must contain Chunfe and no example executable or example test.
- Independently opened examples must configure without configuring the root first.
- QML imports and qmlls metadata must work for each independent build directory.
- Source moves must preserve application installation names and packaging inputs.
- Existing fonts/module/experiment tests must remain runnable by their owning projects.

## Tasks

- [x] Establish existing build/test baseline; preserve the current index state.
- [x] Create `cmake/ChunfeQmlTooling.cmake` with reusable editor setup and metadata dependency handling.
- [x] Add `src/app/main.cpp`, `src/gui/Main.qml`, `src/core/README.md`, and `src/CMakeLists.txt`; make root build Chunfe and its shared dependencies only.
- [x] Make `examples/text_block_lab` standalone; make consumer use repository dependency defaults; isolate test options and source paths.
- [x] Update application CI entry detection, install rules, and Windows QML scan path without adding example CI jobs.
- [x] Update root/module/font/example documentation and ignore independent build directories.
- [x] Build and lint root, lab, and consumer separately; run all existing Qt/Python tests; inspect target graphs; smoke-launch the application and consumer.
- [x] Review the complete diff and report results and platform limitations.

## Execution Notes

- Work in the existing `dev` checkout because it contains the user's current in-progress structure. Preserve its index and unrelated files.
- Configuration and minimal shell changes are verified by real configure/build/lint/run checks and existing regression suites rather than tests that duplicate CMake implementation details.
- Example changes are delegated with exclusive ownership of `examples/**`; root and shared tooling remain with the main agent.

## Verification Results

- Independent main/lab/consumer configurations and builds succeeded in `build/structure-main`, `build/structure-lab`, and `build/structure-consumer`.
- QML lint: all three projects plus the shared TextBlock module passed without warnings.
- CTest: main 3/3, lab 5/5, consumer 2/2; Python release tests 43/43.
- All three executables passed offscreen startup with fatal warnings enabled, then native Cocoa/Metal startup outside the desktop-restricted sandbox. Consumer also produced a rendered screenshot.
- macOS installation completed without deployment warnings; the installed application passed Cocoa/Metal startup without development Qt/QML path overrides.
- Existing root debug cache reconfigured successfully and contains no example targets.
- Independent read-only implementation review returned no actionable findings.
- Windows/Linux native builds and package creation were not run on this macOS host.

## Approved Internal CMake Split

The user selected A: `app`, `core`, and `gui` each own a CMake file as internal subdirectories of the one root project. `core` must not build an empty library. The main application and both examples must be independently built and run again after this split.

- [x] Reduce `src/CMakeLists.txt` to ordered subdirectory inclusion.
- [x] Move executable, metadata and deployment ownership to `src/app/CMakeLists.txt`.
- [x] Define the `chunfe_gui` static QML module in `src/gui/CMakeLists.txt`, preserving URI `Chunfe`; explicitly link and register its static plugin in the app.
- [x] Add a documented `src/core/CMakeLists.txt` without a target until business sources exist.
- [x] Update documentation for target ownership and the GUI lint target.
- [x] Reconfigure and build each project independently; run QML lint, CTest and native startup checks.
- [x] Verify the installed application still resolves its QML module and deployed Qt dependencies; review the final change.

### Internal Split Verification

- Reconfigured and built main, lab and consumer independently after the target split. QML lint remains warning-free; CTest remains main 3/3, lab 5/5, consumer 2/2.
- Verified that the root graph includes `chunfe_gui` and `chunfe_guiplugin`, excludes examples, and does not generate `chunfe_core`.
- Read-only review identified a Qt 6.12 incremental deployment issue: extra scanner roots were not file dependencies. Reproduced stale import metadata after adding `QtQuick.Dialogs`; propagated the GUI QML file list to the app's scanner dependencies. Adding and removing the temporary import now each rerun scanning and update deployment metadata. Restored the original source and rebuilt/linted/tested successfully.

- Final native checks passed for all three projects on Cocoa/Metal with fatal warnings enabled; the consumer generated a fresh screenshot. A fresh installed Chunfe bundle also started on Cocoa/Metal without development Qt/QML path overrides. No deployment warnings were reported.
