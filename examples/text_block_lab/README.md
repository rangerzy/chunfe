# TextBlockLab

TextBlockLab is a standalone Qt 6.12 / CMake 3.24 project for exploring TextBlock layout and interaction. The repository's root project does not build this example.

From the repository root:

```sh
cmake -S examples/text_block_lab -B build/text-block-lab -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12/macos
cmake --build build/text-block-lab
```

Use the Qt installation prefix for your platform. The executable is placed in `build/text-block-lab`.

The example defaults to the repository's `modules/text_block` and `resources/fonts` directories. Override `TEXT_BLOCK_SOURCE_DIR` and `CHUNFE_FONTS_SOURCE_DIR` with absolute paths to build against another checkout or after copying the example elsewhere. The lab requires the font bundle.

Tests are opt-in and use the same independent build directory:

```sh
cmake -S examples/text_block_lab -B build/text-block-lab \
  -DTEXT_BLOCK_LAB_BUILD_TESTS=ON -DTEXT_BLOCK_BUILD_TESTS=ON
cmake --build build/text-block-lab
ctest --test-dir build/text-block-lab --output-on-failure
```

`TEXT_BLOCK_LAB_BUILD_TESTS` controls the lab's tests. `TEXT_BLOCK_BUILD_TESTS` separately controls the reusable module's tests; either can be enabled alone. Configure first with the Qt prefix shown above, or include it when configuring tests in a fresh build directory.
