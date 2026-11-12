# CMake 4.x build baseline

Approved scope: the user accepted all migration recommendations and authorized
implementation, delegating the stable 4.x minimum-version selection.

- Require CMake 4.3.6 throughout every repository-owned standalone entry point,
  including examples and the custom miniaudio wrapper. Use its NEW policies.
- Use 4.3.6 as the reproducible CI baseline and 4.4.5 for newer-release checks.
  These are the previous and latest stable releases listed on cmake.org.
- Synchronize presets, current build documentation, and CI. Keep historical
  design records unchanged.
- Remove unused global definitions, clarify C/C++ language ownership and target
  dependencies, and simplify redundant internal project declarations.
- Preserve Qt 6.12 tooling fixes, Qt Creator integration, standalone consumer
  behavior, application versions, packaging outputs, and existing user edits.
- Use Qt BIG_RESOURCES for the existing font bundle to avoid generated C++ data
  arrays; preserve all resource names, paths, font contents, and initialization.
- Provision CMake inside the Flatpak build sandbox; a host installation cannot
  establish the tool used by the SDK.
- Verify the root application and standalone examples with real configure/build
  commands, run their available tests, and verify rejection of older CMake.

Alternatives considered: changing only the root version leaves standalone
entry points inconsistent; rewriting unrelated Qt/consumer support removes
working features. The chosen scope modernizes the owned build configuration.

Sources: https://cmake.org/download/ and
https://cmake.org/cmake/help/v4.3/command/cmake_minimum_required.html.
