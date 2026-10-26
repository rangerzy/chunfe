# Version Tag Release Implementation Plan

> **For agentic workers:** Use the approved design and test-driven implementation. User explicitly authorized implementation; no further design approval is pending.

**Goal:** Publish a distinct release for each manually increased four-part version.
**Architecture:** Shared version source/reader; main-push tag-and-dispatch coordinator; tag-only builds and immutable publisher.
**Tech Stack:** CMake, Python standard library/unittest, GitHub Actions, existing macOS/Windows/Flatpak scripts.
**Spec:** docs/superpowers/specs/2026-10-23-version-tag-release-design.md

## Global Constraints
- Four numeric version components; main changes only; built-in token + workflow_dispatch(ref=tag).
- Never move tags or overwrite published Release assets/metadata; failed draft resumes for same tag/SHA.
- Preserve unrelated dirty/index state. Work in the managed worktree, then transfer only verified changed files.

## Review Focus
- Tag creation succeeds but dispatch fails; rerun uses the same tag/SHA.
- Multiple pushes/retries and duplicate versions do not move tags or overwrite a published release.
- Legacy three-part versions and rewritten old history do not break first new release.
- Source, tag, compiled version and all artifact filenames agree.
- API 401/403/422/network errors are not treated as missing release/tag.

## Task 1: Shared version and platform packaging
Files: cmake/AppVersion.cmake, scripts/ci/app_version.py, CMakeLists.txt, src/app/CMakeLists.txt, platform packaging scripts, tests/test_app_version.py.
- [x] Add and run failing version/parser/platform validation tests.
- [x] Extract the user's existing four-component defaults; implement the shared interface from the spec.
- [x] Adapt platform artifact handling and bundle/resource metadata.
- [x] Run focused tests and CMake/script validation.

## Task 2: Tag coordinator and workflows
Files: .github/scripts/tag_version.py, .github/workflows/version.yml, .github/workflows/build.yml, tests/test_tag_version.py.
Consumes the shared version reader and parse_version signature above.
- [x] Add failing main-change, legacy migration, duplicate-tag, regression, retry, and failed-dispatch tests against a real temporary git history and mocked GitHub process boundary.
- [x] Implement create-only tag and dispatch(ref=tag), safe existing-tag checks, source-version change detection.
- [x] Restrict build to tags, validate declared version and run publisher tests before three-platform builds.
- [x] Run focused tests and workflow validation.

## Task 3: Immutable publisher
Files: .github/scripts/publish_release.py, tests/test_publish_release.py.
Consumes shared version reader and tag environment contract.
- [x] Replace old mutable-release tests with failing tests for tag/source/artifact agreement, no tag writes, published no-op, resumable drafts and asset integrity.
- [x] Implement approved publisher behavior, including no changes to published releases and numeric Latest comparison supporting legacy tags.
- [x] Run focused tests.

## Task 4: Integration, docs and review
Files: README.md, packaging/linux/README.md and plan/spec as needed.
- [x] Document exact developer version-change/retry flow and token permissions; remove mutable-release instructions.
- [x] Run all Python tests, relevant shell/CMake validation and fresh independent review.
- [x] Resolve actionable findings with regression tests, transfer verified changes to original workspace, verify untouched user files and index.

## Progress
- Baseline: 43 existing publisher tests pass in the isolated worktree.
- Worktree: /Users/yongzhang/.codex/worktrees/version-tag-release/chunfe; original user CMake edits copied as starting point.

- Task 1: shared four-part version and packaging implemented; 20 focused tests pass, including Linux staged-version matching and installed checksum validation.
- Task 2: coordinator implemented; 16 focused tests pass, covering partial creation/dispatch failures and source history migration.
- Task 3: publisher implemented; 40 focused tests pass. Independent review found the old-SHA target_commitish permission issue; default-branch metadata plus unique SHA marker fix passed red-green tests and follow-up review.
- Integrated initial suite: 68 passed. YAML parse passes. actionlint 1.7.12 passes other checks with its unsupported queue field diagnostic narrowly filtered; queue:max was checked against current official GitHub docs.
- Ruling: repository-wide publication queue:max replaces per-tag publication concurrency to prevent cross-version Latest races while retaining waiting versions (official GitHub maximum100).
- Ruling: release target_commitish is default-branch metadata, never the immutable tag binding. Draft body marker and actual remote tag SHA bind the commit, allowing built-in-token retries after workflow files evolve.

## Final verification
- Full Python suite: 76 tests passed.
- Native macOS: configured and built with Qt 6.12.0; all three CTest suites passed. Generated version.txt and bundle metadata match 0.1.0.0 and its documented macOS mapping.
- Shell syntax, workflow YAML parsing, and git diff --check passed. actionlint 1.7.12 passed with only its unsupported queue diagnostic filtered; queue:max is supported by current official GitHub documentation.
- Fresh independent review completed; its one actionable finding was fixed and the scoped follow-up review found no new issue.
- No live tag, release, commit, or push performed. Native Windows/Flatpak packaging and remote dispatch require GitHub CI after merge.
