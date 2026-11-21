# ShutterFX

A native OpenFX plugin for DaVinci Resolve, the foundation for a cinematic slow-shutter
motion-blur engine.

**Status: Milestone 01 (0.1.0).** ShutterFX loads as an OFX image effect and renders the source
unchanged. There is no motion blur yet; `Enable` and `Mix` are wired up but have no visible effect
because the processing stage is the identity. See [Known limitations](#known-limitations).

| | |
|---|---|
| Plugin ID | `com.shutterfx.motionblur` |
| Label / group | `ShutterFX` / `ShutterFX` |
| Version | 0.1.0 |
| Formats | RGBA 8-bit, 16-bit, 32-bit float |
| OpenFX | official SDK, tag `OFX_Release_1_4_TAG` (git submodule) |
| Language / build | C++17, CMake >= 3.20 |
| macOS | deployment target **11.0**, universal `arm64` + `x86_64` |

## Build (macOS)

Requirements: Xcode Command Line Tools (`xcode-select --install`), CMake (`brew install cmake`),
optionally Ninja (`brew install ninja`).

```bash
git clone --recurse-submodules https://github.com/Hash144/ShutterFX.git
cd ShutterFX
packaging/build_macos.sh Release
```

This configures, builds, runs the unit tests, ad-hoc signs the bundle, and verifies its structure.
Output: `build/Release/ShutterFX.ofx.bundle`.

Useful options (append to the command): `-DCMAKE_OSX_ARCHITECTURES=x86_64` (Intel only),
`-DCMAKE_OSX_DEPLOYMENT_TARGET=12.0`, `-DSHUTTERFX_SANITIZE=ON` (ASan/UBSan, Debug), 
`-DSHUTTERFX_WERROR=OFF` (not recommended). Use `Debug` instead of `Release` for a debug build.

If you cloned without `--recurse-submodules`: `git submodule update --init --recursive`.

### Bundle layout

```text
ShutterFX.ofx.bundle/
└── Contents/
    ├── Info.plist
    └── MacOS/
        └── ShutterFX.ofx      <- universal binary (OFX requires the .ofx suffix)
```

## Install

```bash
packaging/install.sh            # user install: ~/Library/OFX/Plugins (no admin rights)
packaging/install.sh --system   # system install: /Library/OFX/Plugins (uses sudo)
packaging/install.sh --uninstall
```

The script only ever touches `ShutterFX.ofx.bundle`; other plugins are left alone.

> The OFX specification names `/Library/OFX/Plugins` as the default macOS search location.
> Whether Resolve also scans `~/Library/OFX/Plugins` has **not been verified** for Resolve 21.1.1.
> If ShutterFX does not show up after a user install, use `--system`.

### Find it in Resolve

1. Quit DaVinci Resolve completely (**Cmd+Q**), then start it again.
2. Open the **Color** or **Edit** page and open the **Effects** panel (Effects Library).
3. Expand **OpenFX**; ShutterFX appears under the **ShutterFX** group.
4. Drag it onto a clip (Edit page) or a node (Color page). Controls: **Enable**, **Mix (%)**, and
   a collapsed **Information** section (plugin name, version).

Resolve does not report OFX load failures prominently; if the plugin is absent, look in the Resolve log
(typically under `~/Library/Application Support/Blackmagic Design/DaVinci Resolve/logs/`).

## Develop / test without a Mac

The host-independent code and the OFX entry points (against a mock host) build and run anywhere:

```bash
cmake -S . -B build/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSHUTTERFX_SANITIZE=ON
cmake --build build/dev
(cd build/dev && ctest --output-on-failure)
```

On Linux this produces a development-only layout (`Contents/Linux-x86-64/`) used for compile checks.
It is not a supported install target. The mock host is **not** a substitute for testing in Resolve.

## Architecture

- `src/utilities/ImageUtils.*` - OFX-free image views, stride/origin-aware `copyRegion` and
  `mixRegion`. Never writes outside the destination, zero-fills uncovered pixels.
- `src/plugin/ShutterFXFactory.cpp` - exports `OfxGetNumberOfPlugins` / `OfxGetPlugin`, fetches
  suites on Load, dispatches actions, converts exceptions to status codes.
- `src/plugin/ShutterFXPlugin.cpp` - describe / describe-in-context, instance, `IsIdentity`, render.
  Future processing modules plug in where `render()` currently passes the source through.

Actions implemented: Load, Unload, Describe, DescribeInContext, CreateInstance, DestroyInstance,
Render, IsIdentity (reports identity only when Enable is off or Mix is 0). Everything else returns
`kOfxStatReplyDefault`.

## Known limitations

- **No motion blur yet.** Output equals input. Enable/Mix are inert visually.
- **Resolve compatibility is unverified until `TEST_RESULTS.md` says otherwise.**
- CPU rendering only; half-float (`OfxBitDepthHalf`) is not declared, and is refused if sent.
- RGBA only (no RGB/Alpha-only components). Source and output depths must match.
- No temporal clip access, no GPU/Metal path, no custom UI/overlays.
- Ad-hoc code signing only: fine for local use; distribution would need a Developer ID signature
  and notarisation.
- Universal build: the `arm64` slice cannot be executed on an Intel Mac, so it is built and
  inspected but not run there.
