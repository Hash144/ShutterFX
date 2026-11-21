#!/usr/bin/env bash
# Verify the structure of a ShutterFX OFX bundle (and, on macOS, its binary).
# Usage: packaging/verify_bundle.sh <path/to/ShutterFX.ofx.bundle>
set -u

BUNDLE="${1:-}"
if [ -z "$BUNDLE" ]; then echo "usage: $0 <ShutterFX.ofx.bundle>" >&2; exit 2; fi

fail=0
ok()  { printf '  [ ok ] %s\n' "$1"; }
bad() { printf '  [FAIL] %s\n' "$1"; fail=1; }

echo "Verifying: $BUNDLE"
[ -d "$BUNDLE" ] && ok "bundle directory exists" || { bad "bundle directory missing"; exit 1; }
case "$(basename "$BUNDLE")" in ShutterFX.ofx.bundle) ok "bundle name is ShutterFX.ofx.bundle";; *) bad "unexpected bundle name";; esac

[ -f "$BUNDLE/Contents/Info.plist" ] && ok "Contents/Info.plist present" || bad "Contents/Info.plist missing"

if [ "$(uname -s)" = "Darwin" ]; then
  BIN="$BUNDLE/Contents/MacOS/ShutterFX.ofx"
  [ -f "$BIN" ] && ok "Contents/MacOS/ShutterFX.ofx present" || { bad "Contents/MacOS/ShutterFX.ofx missing"; exit 1; }

  for key in CFBundleExecutable CFBundlePackageType CFBundleInfoDictionaryVersion CFBundleVersion CFBundleDevelopmentRegion; do
    /usr/libexec/PlistBuddy -c "Print :$key" "$BUNDLE/Contents/Info.plist" >/dev/null 2>&1 \
      && ok "Info.plist key $key" || bad "Info.plist key $key missing"
  done
  exe="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$BUNDLE/Contents/Info.plist" 2>/dev/null)"
  [ "$exe" = "ShutterFX.ofx" ] && ok "CFBundleExecutable matches binary name" || bad "CFBundleExecutable ('$exe') != ShutterFX.ofx"
  pkg="$(/usr/libexec/PlistBuddy -c 'Print :CFBundlePackageType' "$BUNDLE/Contents/Info.plist" 2>/dev/null)"
  [ "$pkg" = "BNDL" ] && ok "CFBundlePackageType is BNDL" || bad "CFBundlePackageType ('$pkg') != BNDL"
  plutil -lint "$BUNDLE/Contents/Info.plist" >/dev/null 2>&1 && ok "Info.plist is valid" || bad "Info.plist is not valid"

  archs="$(lipo -archs "$BIN" 2>/dev/null)"
  echo "  [info] architectures: ${archs:-unknown}"
  [ -n "$archs" ] && ok "lipo can read the binary" || bad "lipo cannot read the binary"
  file "$BIN" | sed 's/^/  [info] /'

  syms="$(nm -gU "$BIN" 2>/dev/null | awk '{print $NF}' | grep -E '^_Ofx' | sort | tr '\n' ' ')"
  echo "  [info] exported OFX symbols: $syms"
  case "$syms" in *_OfxGetNumberOfPlugins*) ok "exports OfxGetNumberOfPlugins";; *) bad "OfxGetNumberOfPlugins not exported";; esac
  case "$syms" in *_OfxGetPlugin*) ok "exports OfxGetPlugin";; *) bad "OfxGetPlugin not exported";; esac

  codesign --verify --deep --strict "$BUNDLE" >/dev/null 2>&1 && ok "code signature valid" || bad "code signature missing/invalid (arm64 will not load unsigned code)"
  otool -l "$BIN" 2>/dev/null | grep -A3 LC_BUILD_VERSION | grep minos | sort -u | sed 's/^ */  [info] /'
else
  echo "  [info] not macOS: checking the development (compile-check) layout only"
  BIN="$(ls "$BUNDLE"/Contents/*/ShutterFX.ofx 2>/dev/null | head -n1)"
  [ -n "$BIN" ] && ok "binary present: ${BIN#$BUNDLE/}" || { bad "no Contents/<arch>/ShutterFX.ofx"; exit 1; }
  nm -D --defined-only "$BIN" 2>/dev/null | grep -q ' T OfxGetNumberOfPlugins' && ok "exports OfxGetNumberOfPlugins" || bad "OfxGetNumberOfPlugins not exported"
  nm -D --defined-only "$BIN" 2>/dev/null | grep -q ' T OfxGetPlugin' && ok "exports OfxGetPlugin" || bad "OfxGetPlugin not exported"
fi

if [ "$fail" -eq 0 ]; then echo "RESULT: bundle structure OK"; else echo "RESULT: FAILED"; fi
exit "$fail"
