# Native UI Polish Implementation Plan

**Goal:** Apply user-confirmed menu states, official IconPark icons, clearer text and clean product copy.

**Spec:** `docs/superpowers/specs/2026-11-03-native-ui-polish.md`

**Scope:** Preserve the Qt6.12 application/module boundaries and all in-memory functionality.

- [x] Vendor official IconPark outline SVGs with source/license records, replace AppIcon while preserving public name/color/size API.
- [x] Make sidebar selected/hover/pressed visibly distinct; keep fixed rail and focus/hover exclusivity.
- [x] Increase text contrast and modest type sizes; inspect render behavior before changing rasterization globally.
- [x] Remove nonfunctional copy and developer controls from pages, shell, floating panels and notifications; retain product states/errors.
- [x] Build, lint, exercise existing UI tests and inspect updated rendered pages and hover state. Restart desktop preview.

Parallel ownership: icon resources/AppIcon, application pages, read-only text rendering investigation; root owns shell, shared controls, panels, mock copy and final integration.

## Verification

- Official @icon-park/svg 1.4.2: 30 SVGs generated with the official renderer; source archive SHA-256, gitHead, aliases and individual source hashes in icons/SOURCE.json; Apache license embedded with the module. Isolated software-render probe verified dark, muted and white colors without shaders.
- Qt6.12 build and all four relevant QML lint targets passed. Full CTest 5/5 passed; native UI checks17/17 passed including optional page/menu/floating-panel rendering. No new QML binding or image loading warnings (offscreen raise warning is platform-limited as before).
- Kept QtRendering and the existing fonts. Adopted the already verified lab grayscale antialiasing setting at application startup, respected environment overrides, darkened colors and removed paused-text fading. Did not claim desktop hardware blur eliminated based on offscreen renders.
- UI source scan found no remaining demo/simulation labels or decorative hand-authored icon paths. App-only mock machinery and documented storage limits remain intact.
- Updated native desktop preview launched. Rendered images: build/debug/ui-polish-previews/.
