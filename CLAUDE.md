# ShutterFX - project rules for Claude

Native OpenFX plugin for DaVinci Resolve (macOS): a future cinematic slow-shutter
motion-blur engine. **Current milestone: 01 - OFX foundation (identity render only).**

## Hard rules (from Job Card 001)
- C++17 + CMake only. No Python in the plugin. No Metal. No optical-flow dependencies.
- Use the official OpenFX C API headers (`external/openfx`, pinned to `OFX_Release_1_4_TAG`).
  Do not invent substitutes for OFX interfaces.
- Do not implement motion blur / temporal blending until a later milestone says so.
- Build with warnings as errors (`SHUTTERFX_WERROR=ON`); never silence a warning to get green.
- Never claim a test passed without evidence; never claim Resolve compatibility unless it was
  actually run in Resolve. Record results in `TEST_RESULTS.md`.
- Do not start the next milestone unprompted.

## Layout
- `src/utilities/` host-independent image code (no OFX includes) - unit tested on any OS.
- `src/plugin/` OFX glue: `ShutterFXFactory.cpp` (exports, action dispatch),
  `ShutterFXPlugin.cpp` (describe, instance, render), `PluginParameters.h` (ids, params).
- `tests/` `test_image_utils` (buffers) and `test_ofx_entry` (real entry points vs a mock host).
- `packaging/` `build_macos.sh`, `verify_bundle.sh`, `install.sh`.

## Conventions
- Bundle binary must be `Contents/MacOS/ShutterFX.ofx` (OFX spec: `NAME.ofx`).
- Version has one source: `project(VERSION ...)` in `CMakeLists.txt`.
- Only `OfxGetNumberOfPlugins` and `OfxGetPlugin` are exported (hidden visibility otherwise).
- Never let a C++ exception cross the OFX boundary; images acquired from the host are released
  via RAII (`ClipImage`).
- Linux can compile-check and run the tests; the real bundle and Resolve checks need macOS.

## Commands
```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSHUTTERFX_SANITIZE=ON
cmake --build build/dev && (cd build/dev && ctest --output-on-failure)
packaging/build_macos.sh Release     # macOS: build + test + verify universal bundle
packaging/install.sh                 # macOS: install to ~/Library/OFX/Plugins
```
