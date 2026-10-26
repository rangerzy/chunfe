# Application fonts

## Agreed behavior

- Chinese and common Chinese punctuation use OPPO Sans 4.0, default Regular/400. Keep the original font coverage: symbols already supplied by JetBrains Mono, including full-width braces, remain in that font.
- Latin text, ASCII digits and half-width punctuation use JetBrains Mono.
- Keep existing heading weights and bold emphasis. Emoji retain platform fallback.
- Bundle original font files and licenses under `resources/fonts/`; no system installation or runtime download.
- Load one application font configuration before QML creation. TextBlock keeps accepting a caller-provided QFont and has no dependency on the brand fonts.
- Keep the custom/native comparison and benchmark on the same font configuration.

## Work and verification

- [x] Add resource target, startup registration and visible font attribution.
- [x] Verify actual glyph runs for both families, common punctuation and weights 400/600/700.
- [x] Verify QML Text, Qt.font(), Controls and TextBlock inherit the font configuration.
- [x] Build, lint, run relevant tests and inspect the native macOS output.
- [x] Document resource management and remaining platform limits.
- [x] Resolve Q15: retain the original fonts and accept their symbol-coverage exceptions.

## Evidence

- OPPO archive contains one original variable TTF (weight 100–700, default400) and its license. Preserve both unchanged.
- JetBrains Mono downloaded from the official v2.304 release, using Regular/SemiBold/Bold for the current weight hierarchy.
- User confirmed Q13 and Q14: default Regular with existing bold retained; common punctuation follows the full-width/half-width split. Q15 subsequently confirms original-font symbol exceptions.
- Resource files and original license notices were verified byte-for-byte against both source archives. A user-readable Font Licenses dialog is connected in the application and the optional project-font consumer.
- Actual glyph-run tests pass for Chinese, Latin, adjacent digits, common full-width punctuation, ASCII punctuation and weights 400/600/700. QML Text, Qt.font(), Button, ComboBox, Switch, MenuItem, ToolTip and TextBlock retain the two-family application configuration.
- Review exposed an exception not covered by the common-punctuation tests: JetBrains Mono itself contains U+FF5B/U+FF5D (full-width braces). A Latin-first fallback list uses JetBrains Mono for those glyphs. The user explicitly accepted original-font exceptions in Q15; no derived fonts or universal full-width routing are required. The resource guide and application README document this rule, and a glyph-run regression covers the shared symbols at normal, heading and bold weights.
- Debug build, both QML lint targets and all four CTest executables pass after the license-dialog integration. The consumer builds and renders both with the font target and with `CHUNFE_FONTS_SOURCE_DIR=` (module + Qt only).
- Native macOS output was inspected with the new fonts; Windows/Linux and other display scales remain unverified. Existing native-style customization warnings from ActionButton are outside this font change.
