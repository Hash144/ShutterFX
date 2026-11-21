# Test results - Milestone 01

Legend: **PASS** = run, evidence below. **NOT RUN** = could not be run in the authoring session
(Linux container, no Mac, no Resolve). Nothing marked NOT RUN should be assumed to work.

## Job-card checks

| # | Check | Status | Evidence / notes |
|---|---|---|---|
| 1 | CMake configuration succeeds | PASS on Linux; **NOT RUN on macOS** | GCC 13 and Clang, Ninja, Debug and Release |
| 2 | Compilation succeeds | PASS on Linux; **NOT RUN on macOS** | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Werror`, zero warnings on GCC and Clang |
| 3 | Binary architecture is correct | **NOT RUN** | needs macOS: `lipo -archs` (expect `x86_64 arm64`); done by `verify_bundle.sh` |
| 4 | OFX bundle structure valid | Linux dev layout PASS; **macOS bundle NOT RUN** | `verify_bundle.sh` on the Linux layout; macOS checks (Info.plist keys, `plutil`, `codesign`) implemented but not yet executed |
| 5 | Installed in expected directory | Installer logic PASS on Linux (`--dest`); **real macOS install NOT RUN** | install / reinstall / uninstall; unrelated plugin untouched; error on missing bundle |
| 6 | Resolve detects the plugin | **NOT RUN** | |
| 7 | Appears in the Effects Library | **NOT RUN** | |
| 8 | Can be applied to a clip | **NOT RUN** | |
| 9 | Output matches the original | Buffer level PASS (mock host); **in Resolve NOT RUN** | bit-exact comparison in `test_ofx_entry`, all three depths |
| 10 | No Resolve crashes | **NOT RUN** | |
| 11 | No obvious memory errors | PASS for the test harness on Linux | ASan+UBSan clean; Valgrind `--leak-check=full` clean; guard-byte checks around every output buffer; every acquired image released. Does not cover Resolve's own behaviour |

## Automated test runs (Linux x86_64, Ubuntu 24.04)

| Suite | Result |
|---|---|
| `test_image_utils` (GCC Debug + ASan/UBSan; Clang Release; Valgrind) | 87 checks, 0 failures |
| `test_ofx_entry` (GCC Debug + ASan/UBSan; Clang Release; Valgrind) | 137 checks, 0 failures |
| Mutation check (copy one byte short) | detected: 21 failures, so the tests can fail |
| `nm -D` on the built `.ofx` | only `OfxGetNumberOfPlugins` and `OfxGetPlugin` exported |

`test_ofx_entry` drives the real `OfxGetPlugin()->mainEntry` through a mock host and covers:
Load/Describe/DescribeInContext (labels, grouping, depths, clips, param defaults, info group),
CreateInstance, Render for Byte/Short/Float with odd sizes, non-zero origins, differing and padded
row strides, partial and oversized render windows, Enable off, Mix 0/37.5/250/-5, IsIdentity,
and failure paths (half-float, RGB, depth mismatch, null data, bad row bytes, missing clips, image
fetch failure, missing render window, actions before Load, missing host suites) with a check that no
image is left unreleased.

**Limits of this evidence:** the mock host is written by the same author as the plugin, so it checks
internal consistency and the spec as understood, not Resolve's actual behaviour. Real Resolve
behaviour (which pixel formats it sends, plugin search path, signing requirements) is untested.

## Manual verification on your Mac (to complete the milestone)

Run in a terminal:

```bash
git clone --recurse-submodules https://github.com/Hash144/ShutterFX.git && cd ShutterFX
packaging/build_macos.sh Release          # builds, runs tests, verifies bundle; paste the output here
packaging/install.sh                      # user install
```

Then in Resolve 21.1.1:

- [ ] Quit Resolve (Cmd+Q) and relaunch.
- [ ] Effects Library > OpenFX > ShutterFX is listed. If not: `packaging/install.sh --system`, relaunch.
- [ ] Apply to a clip on the Edit page (and a node on the Color page); the controls Enable, Mix (%), Information appear.
- [ ] Image is unchanged with Enable on/off and Mix at 0, 50, 100 (toggle the effect, compare; use a
      split-screen or the Difference/A-B in Resolve, or a full-range test chart; scopes should be identical).
- [ ] Check with an 8-bit (H.264) clip, a 10-bit clip and a float/EXR clip if available; a clip with odd
      dimensions; a clip at a non-standard timeline resolution; changing project colour management.
- [ ] Play and scrub for a minute, change params while playing; no crash, no hang.
- [ ] Remove the effect, save/reopen the project; no crash.
- [ ] Record: macOS version, `lipo -archs` output, Resolve version, and any console/log messages.

Fill in the results below.

| Check | Result | Notes |
|---|---|---|
| macOS version / Xcode CLT / CMake | | |
| `build_macos.sh` output (pass/fail) | | |
| `lipo -archs` | | |
| Detected in Resolve (user path / system path) | | |
| Applies to clip | | |
| Output identical | | |
| Crashes | | |
