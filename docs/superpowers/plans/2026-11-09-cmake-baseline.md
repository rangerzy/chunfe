# CMake 4.x Baseline Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans for the root changes;
> delegate independent CI and documentation files to focused agents.

**Goal:** Require stable CMake 4.3.6 across the project and validate newer 4.4.5.

**Architecture:** Use normal minimum-version declarations to establish NEW
policies. Keep targets scoped, preserve standalone consumers, and provision the
actual CMake used by release jobs and the Flatpak sandbox.

**Tech Stack:** CMake 4.3.6/4.4.5, Ninja, Qt 6.12, C/C++23, GitHub Actions, Flatpak.

**Spec:** docs/superpowers/specs/2026-11-09-cmake-baseline-design.md

## Global Constraints

- Minimum CMake 4.3.6; newer-version validation uses 4.4.5.
- Preserve existing uncommitted work; edit this working tree without committing.
- Preserve Qt/tooling fixes, consumer independence, and packaging behavior.

## Review Focus

- Older CMake must fail before configuration; tests must use the selected tool.
- Internal subdirectories must not reset project identity or policy baseline.
- Miniaudio must compile as C with platform dependencies intact.
- Copied consumer examples must retain their optional resources/tooling behavior.
- Flatpak must use provisioned CMake and exclude build tools from the final app.

## Tasks

- [x] **1. Build configuration:** Update all owned entry points and presets;
  remove unused global C_VERSION definition; order miniaudio before dependents;
  simplify audio/internal projects while retaining standalone builds; correct
  target-scoped standards and optional font linking in the old calendar sample.
  Verify using 4.3.6 configure/build/CTest plus an older-version rejection check.
- [x] **2. CI:** Pin release tools, add minimum/newer validation, provision the
  Flatpak CMake module with an upstream checksum, and check the tool used inside
  the SDK. Add focused behavioral tests for any new validation script.
  Verify Python tests, shell syntax, YAML/JSON structure, and toolchain checks.
- [x] **3. Documentation:** Update current prerequisites and describe pinned CI
  and sandbox tooling; retain historical documents.
- [x] **4. Integration:** Build root and examples, run existing tests, review
  task-only changes against the baseline snapshot, and fix actionable findings.

## Execution record

- Worktree: in-place on dev because this change includes existing uncommitted
  audio integration; a clean checkout would omit those authorized inputs.
- No automatic commit, branch, push, or publication is part of this task.
- Baseline: 76 Python tests and the original 3.30.5 configuration passed.
- 4.3.6 and 4.4.5 configure the root; 76 Python tests pass with each version.
- All seven independent entry points reject the original 3.30.5 CMake.
- Standalone miniaudio/audio and both copied, font-free consumers build.
- Review found and fixed embedded miniaudio C-language activation; a CXX-only
  parent failed before the fix and configured/built successfully afterward.
- Workflow YAML, Bash syntax, JSON, and published archive checksums validated;
  actual Windows/Linux/Flatpak runs require their target environments.
- Ruling: enable Qt BIG_RESOURCES for the font bundle. Actual builds generated
  a 174 MB C++ source and spent minutes under memory pressure; direct resource
  object generation preserves all font data and avoids that compiler workload.
  Supported targets use Ninja and CMake 4.3.6, above Qt's required 3.17 floor.
- Final runtime verification: both 4.3.6 and 4.4.5 build the root application
  and all four examples. All five root CTests and nine example CTests pass per
  version. The prototype's 51 QML cases all pass.
- CI coverage exposed three stale prototype assertions expecting no October
  markers, while the unchanged demo provides 2026-10-25. Direct Qt invocation
  reproduced the failure without CMake. Updated only those test expectations;
  production QML is unchanged.
- Release jobs explicitly select matching C and C++ compilers for the newly
  declared C language (Apple Clang / Qt MinGW), avoiding host-tool selection.
