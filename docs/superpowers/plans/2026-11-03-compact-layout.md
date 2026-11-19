# Compact Native Layout Plan

Spec: `docs/superpowers/specs/2026-11-03-compact-layout.md` (approved).

- [x] Apply 820×560 default/min 760×520 and compact shared page spacing.
- [x] Add reusable SettingRow to Chunfe.Ui, align model/audio settings with a shared fixed label column and full-width control area.
- [x] Refine HomePage and BackgroundPage, remove duplicate chrome/page titles.
- [x] Check history/detail/preflight at compact sizes, retain accessible scrolling and actions.
- [x] Build and lint; verify default/min geometry and existing interview lifecycle; render and inspect screenshots; launch app.

Implementation interface: SettingRow properties `label`, `description`, `labelWidth`=136, `spacing`=20, `minimumHeight`=42; default content fills right-side control area. `description` appears below controls. Page padding24, section spacing20, FormCard padding20/spacing16. Root coordinates shared changes; parallel agent owns AudioPage and ModelPage only.

Verification: Qt 6.12 debug build and all four QML lint targets passed; all five CTest groups passed. Native UI checks cover shared control edges/heights, stable dimensions while navigating at the minimum size, the failed-preflight dialog fitting the window, and existing floating-window lifecycle. Final homepage alignment adjustment was rebuilt, linted, and passed the native UI group again (19 checks including setup/cleanup). Inspected offscreen Qt screenshots under `build/debug/compact-layout-previews/`, including minimum-size settings, background, record and preflight. Launched the rebuilt desktop app; startup log contains no QML errors. Offscreen captures do not verify operating-system drag or always-on-top behavior.
