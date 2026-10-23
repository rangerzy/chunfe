# TextBlock consumer

This standalone Qt 6.12 / CMake 3.24 project demonstrates using TextBlock in an application. The repository's root project does not build this example.

From the repository root:

```sh
cmake -S examples/text_block_consumer -B build/text-block-consumer -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12/macos
cmake --build build/text-block-consumer
```

Use the Qt installation prefix for your platform. The executable is placed in `build/text-block-consumer`. Keep this build directory separate from the root application and other examples.

`TEXT_BLOCK_SOURCE_DIR` defaults to the repository's `modules/text_block`. Set it to an absolute module source path when copying this example elsewhere or using another checkout:

```sh
cmake -S /path/to/text_block_consumer -B /path/to/consumer-build -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12/macos \
  -DTEXT_BLOCK_SOURCE_DIR=/path/to/modules/text_block
```

The repository's font bundle is included when available. Set `CHUNFE_FONTS_SOURCE_DIR` to another bundle path, or pass `-DCHUNFE_FONTS_SOURCE_DIR=` to use the system font. The copied example needs only Qt and the TextBlock sources when the font path is empty.

Pass `-DTEXT_BLOCK_BUILD_TESTS=ON` to enable the module's tests, then build and run `ctest --test-dir build/text-block-consumer --output-on-failure`.
