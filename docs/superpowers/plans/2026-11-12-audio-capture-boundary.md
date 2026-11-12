# Audio capture responsibility split

> **For agentic workers:** Use superpowers:executing-plans to implement and verify each boundary. The user confirmed the proposed responsibilities and two single-source workers in chat.

**Goal:** Make AudioPiplineManager coordinate the pipeline while captures implements device acquisition and PCM production.

**Architecture:** Two AudioCaptureWorker instances share an AudioCaptureContext and one control thread. Context hides device enumeration/permissions/native handles; workers own individual devices and callback lifetimes. Manager retains queue/session/epoch/boundary orchestration and receives value results.

**Tech Stack:** Existing C++17/Qt/miniaudio/CoreAudio targets.

**Spec:** ../specs/2026-11-11-audio-pipline-interface-design.md

## Constraints

- Preserve public manager methods, signals, Float32 input and shared queue protocol.
- First startup succeeds for all approved sources or rolls back; resume retains stable device IDs and permits existing degraded behavior.
- Keep backend operations off the manager event thread and PCM delivery gated until startup signals finish.
- Stop failure retains callback storage/context safely and supports retry.
- Work in the existing checkout: the implementation being corrected is uncommitted here. Do not stage or commit unrelated work.

## Review focus

- Stop or destruction during device initialization must await cleanup.
- Signal reentry must not reopen capture or attach old events to a new session.
- Both sources must finish starting before either publishes PCM.
- A held queue block remains valid during pause/stop; backend failure must not free callback storage.
- Resume uses the previously selected device, even if the default changes.

## Tasks

- [x] Establish the existing audio test baseline in an isolated build directory.
- [x] Add worker regression cases for PCM delivery/gating, chunking/levels and fixed-device resume; add two-source startup/rollback coverage.
- [x] Implement captures/audio_capture_context.{h,cpp}: initialized, refresh, snapshot and requestPermissions, hiding native context and cached device IDs.
- [x] Implement captures/audio_capture_worker.{h,cpp}: prepare, startCapture, stopCapture, info, setAccepting, monitoring, level and overflow statistics. PCM and native callbacks stay here.
- [x] Replace manager native fields/callbacks with two workers and value snapshots; preserve the existing orchestration regressions.
- [x] Update build source lists, README and the responsibility specification.
- [x] Build the audio library and application; run CTest, independent review and fix any findings.

Validation uses the existing audio test targets in /tmp/chunfe-audio-baseline, with CTest outside the sandbox only when needed for POSIX shared-memory creation.

## Completed validation

- Full application and test build: `cmake --build /tmp/chunfe-audio-baseline -j 4` passed with Qt CMake 4.4.5.
- Complete CTest suite: 5/5 passed (audio manager/worker, shared queue, mac system capture, fonts and UI).
- Python suite: 76/76 passed with the same CMake on PATH. The first run used system CMake 3.30.5 and failed two minimum-version checks; rerunning with the project-compatible tool resolved both.
- Independent Standards and Spec reviews found no actionable new issues.
- Native device control is simulated in manager/worker tests; no live hardware recording was performed.

## Follow-up: explicit manager members

The user requested removing `m_runtime` entirely and exposing clear ownership and session state in the manager class.

- [x] Keep the capture context, queue, two worker pointers and control thread as direct members; preserve Qt thread-finished worker destruction.
- [x] Group direct members by resources, environment requests, session, asynchronous startup, stop/finalization and notifications; do not add a replacement state bag.
- [x] Replace stop booleans with a stop-request enum and a stop target. Derive normal end-marker kind/cancellation; retain uncancelled failure markers.
- [x] Remove duplicate capture/device information caches, retaining per-source formats for failed-resume end markers.
- [x] Add a regression for upgrading an in-flight pause stop to final stop, with exactly one stopped/drained notification.
- [x] Build, run the full test suite and review callback lifetime/state equivalence against the previous implementation.

Follow-up validation: full application build passed; CTest 5/5 and Python 76/76 passed. After restoring explicit zero initialization of both format caches, the application was rebuilt and all three audio CTest targets passed again. Independent architecture and protocol reviews found no outstanding issues.
