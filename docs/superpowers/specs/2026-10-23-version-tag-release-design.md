# Version tag releases — approved design

User approved all recommendations on 2026-10-23 and explicitly requested implementation.

- Developer manually maintains four numeric version components, initially 0.1.0.0.
- Main pushes detect a declared version change. An unchanged version does not build/publish.
- Create a new vMAJOR.MINOR.PATCH.BUILD tag at that main event commit; never force-move an existing tag.
- Use the built-in GITHUB_TOKEN with contents:write and actions:write to explicitly workflow_dispatch build.yml with ref equal to the tag. No extra Secret/PAT/App.
- The build workflow accepts version tags (push and workflow_dispatch), checks source/tag agreement, builds the tagged commit on all three platforms, and publishes only after all three artifacts pass validation.
- The publisher never creates/moves tags. A different commit cannot reuse a version. Failed drafts can resume; a published Release is a read-only no-op, with no asset or metadata replacement.
- Preserve all existing source edits and staged work. No source commit, shared-branch push, tag mutation, or live release is required for this implementation task.

## Decisions within the approved scope

- Centralize existing VERSION_MAJOR/MINOR/PATCH/BUILD defaults in cmake/AppVersion.cmake, included by CMake. A dependency-free Python reader is shared by tag creation, artifact validation and Linux packaging. Preserve local CMake overrides; release builds must match declared values.
- Reject malformed/noncanonical components and version regression. Permit the one-time three-component to four-component migration (0.1.0 -> 0.1.0.0). Old v0.1.0 remains untouched.
- Keep public tag, application version, artifact filenames at four components. Map platform metadata to platform constraints. Version fields are bounded for Windows resource compatibility.
- Compare declared source version against the main push before SHA; recognize the previous literal project VERSION format for migration. Missing predecessor or malformed version fails clearly, rather than silently issuing a release.
- Tag-create/dispatch partial failure is recoverable: rerun verifies existing tag matches the same SHA and dispatches again. Never reinterpret an API/auth/server error as absence.
- Published notes describe the tagged commit and run, avoiding a mandatory comparison to the unrelated old release history. Draft notes bind an exact SHA using a chunfe-release-sha marker. Release target_commitish uses the repository default branch because GitHub disallows GITHUB_TOKEN release writes against old workflow-bearing target commits; this metadata never changes an existing tag. Actual tag SHA is independently verified before any publication.
- Serialize publication repository-wide with concurrency queue: max (up to 100 waiting jobs), preserve Latest for newer versions, and validate remote tag again before publishing. Builds remain parallel across versions.

## Shared interface

scripts/ci/app_version.py:
- parse_version(value: str, *, allow_legacy: bool = False) -> tuple[int, ...]; four components normally, three accepted only for legacy comparison.
- read_version(root: Path) -> str; reads cmake/AppVersion.cmake without Qt/configuring the whole app.
- CLI: python3 scripts/ci/app_version.py prints declared version; --check-tag vX.Y.Z.N validates tag and prints version.

The publisher imports that reader using its repository-relative path and requires GITHUB_REF_TYPE=tag, GITHUB_REF_NAME=vX.Y.Z.N, GITHUB_SHA matching the existing tag.
