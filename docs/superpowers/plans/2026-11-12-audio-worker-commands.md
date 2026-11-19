# Audio pipeline command and acknowledgement plan

**Approved scope:** The user confirmed asynchronous worker commands/replies, queue-owned finalization and explicitly bypassed disabled processing stages. Manager owns resources directly and aggregates session status; no replacement manager/state bag is introduced.

**Architecture:** Keep two single-source workers on the existing control thread. Preparation, native start, confirmation, activation and stop are separate queued commands with immutable capture ID and operation number. Confirmation catches native failures queued while the other source is starting. SharedPcmQueue owns bypass, pending boundary insertion and completion events. A processing-stage mask defaults to no enabled processors because none are wired into the application.

**Contract:** Preserve atomic initial startup, fixed-device resume, source degradation, stale-event isolation, stop retry, shutdown callback storage, and captureStopped → queueDrained → close → Idle ordering. Enabled stages must acknowledge cancelled boundaries; disabled stages are explicitly bypassed. Never steal a claimed block. A pause may be upgraded to final stop while workers or the queue are finishing.

- [x] Read current APIs and verify the existing three audio CTest targets.
- [x] Implement queue lifecycle commands/signals and stage masks, with tests for enabled boundary acknowledgements, disabled bypass, held blocks and full-queue finalization.
- [x] Implement worker slots/results, source-owned stop retry and telemetry; move environment requests into the capture layer.
- [x] Replace manager cross-thread lambdas and queue processing loops with signal connections, commands and result aggregation.
- [x] Migrate manager tests to explicit processor configuration and actual processor acknowledgements; cover cancelled resume/late replies.
- [x] Update API documentation, build the application, run full CTest/Python verification and independently review protocol and ownership.

File ownership during implementation: queue/types/tests with the queue worker; captures/context with the capture worker; manager tests with the test worker; manager/CMake/docs with the primary agent. Existing uncommitted changes remain in place. Baseline plugin snapshot: /tmp/chunfe-before-worker-commands.

Verification completed on 2026-11-12:

- Full Debug application build passed in `/tmp/chunfe-audio-baseline` using CMake 4.4.5 and Qt 6.12.0.
- All five CTest targets passed after the final production/test changes (14.66 seconds). Native shared-memory tests ran outside the filesystem sandbox.
- Python unittest discovery passed all 76 tests with the bundled CMake on PATH.
- Independent review found and resolved startup confirmation, failed-source end-marker loss during pause, and nested error-handler rollback/cleanup races. Regression tests cover these cases, including actual enabled-stage acknowledgements and an injected stop retry.
- Scoped diff whitespace checks passed. No commits were created. Capture integration tests use fake native backends; physical devices were not exercised.
