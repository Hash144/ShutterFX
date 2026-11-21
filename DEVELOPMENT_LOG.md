# Development log

## Milestone 01 - OFX foundation

### Environment findings (authoring session)
| Item | Finding |
|---|---|
| Authoring host | Linux x86_64 (Ubuntu 24.04), cloud container - **not macOS** |
| Compilers | GCC 13.3, Clang (system), CMake 3.28.3, Ninja, Valgrind |
| Xcode CLT / Apple SDK / Resolve | not available in the authoring session |
| Target machine (per user) | MacBook Pro, Intel i9 (x86_64); DaVinci Resolve 21.1.1 |
| macOS version, Xcode CLT, CMake on the Mac | **not inspected** (no Mac connection was possible) |
| Repository | `Hash144/ShutterFX`, empty at start |
| OpenFX SDK | `AcademySoftwareFoundation/openfx`, tag `OFX_Release_1_4_TAG` (newest release tag present), submodule at `external/openfx` |

The Mac could not be attached to the session (it offered no usable folder to cloud sessions), so
all macOS steps from the job card's environment inspection, build, install and Resolve testing
are deferred to the user via `packaging/build_macos.sh` and the checklist in `TEST_RESULTS.md`.

### Decisions
1. **Raw OFX C API with the official headers**, not the OpenFX Support C++ library: fewer build
   dependencies, no exceptions crossing the plugin boundary, and the code stays close to the spec.
2. **Binary named `ShutterFX.ofx`** in `Contents/MacOS/`. The job card listed `ShutterFX`, but the
   OFX packaging spec (`Documentation/ofxPackaging.xml`) requires `NAME.ofx`; the card allows
   adjustments the SDK requires. `CFBundleExecutable` matches.
3. **Install path.** The spec's macOS search path is `/Library/OFX/Plugins` only. The installer
   defaults to `~/Library/OFX/Plugins` as the card requires and offers `--system` as the fallback.
   Resolve's behaviour for the user path is unverified.
4. **Ad-hoc code signing** added as a post-build step (Apple Silicon will not load unsigned code).
5. **Deployment target 11.0**, universal `arm64;x86_64` by default.
6. **Enable / Mix are real**: render computes `mix(source, processed)` with `processed = source`.
   The output is identical for every setting; this is documented rather than hidden.
   `IsIdentity` reports identity only when Enable is off or Mix is 0, so the render path runs
   whenever the effect is active.
7. **Describe sets** Filter + General contexts; Byte/Short/Float; no multiple clip depths; tiles and
   multi-resolution supported (render works on any window and origin); fully thread-safe render;
   no temporal access.
8. **Information group** uses disabled, non-persistent label-mode string params (OFX has no
   dedicated label widget).
9. **Version has one source of truth** (`project(VERSION)`), injected as compile definitions and
   into `Info.plist`.
10. **Symbol hygiene.** `OfxExport` is a bare `extern` on non-Windows, which `-fvisibility=hidden`
    would hide, so the entry points carry an explicit default-visibility attribute. Verified with
    `nm`: only `OfxGetNumberOfPlugins` and `OfxGetPlugin` are exported.

### Stages
1. Environment inspection, SDK fetch (submodule at 1.4 tag), packaging spec read.
2. `ImageUtils` + `test_image_utils` (strides, negative strides, odd sizes, non-zero origins,
   sub-windows, source smaller than destination, invalid inputs, in-place, special floats, mix).
   Built with ASan/UBSan.
3. OFX plugin (factory, describe, instance, render) + mock-host `test_ofx_entry`; warnings as errors
   on GCC and Clang. Clang's `-Wsign-conversion` flagged test code that GCC accepted; fixed.
4. CMake bundle layout, `Info.plist`, ad-hoc signing step, packaging scripts. Installer tested on
   Linux against a scratch directory (`--dest`): install, re-install, uninstall, unrelated plugin
   untouched, missing-bundle error.
5. Documentation.

### Issues found during development
- First mock-host build failed: `OfxHost::fetchSuite` returns `const void*` (test-only; fixed).
- A mutation check (copying one byte too few) was caught by the tests (21 failures), confirming the
  tests can fail.

### Not done / next
- macOS configure/build/`lipo`/`codesign`/install and every Resolve check (see `TEST_RESULTS.md`).
- Milestone 2 (motion blur) deliberately not started.
