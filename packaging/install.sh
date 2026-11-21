#!/usr/bin/env bash
# Install ShutterFX.ofx.bundle for DaVinci Resolve.
#
#   packaging/install.sh                 user install -> ~/Library/OFX/Plugins (no admin rights)
#   packaging/install.sh --system        system install -> /Library/OFX/Plugins (asks for sudo)
#   packaging/install.sh --bundle PATH   install a specific bundle (default: build/Release/...)
#   packaging/install.sh --uninstall     remove ShutterFX from the chosen location
#
# Only ShutterFX.ofx.bundle is ever touched; other plugins are left alone.
set -euo pipefail

NAME="ShutterFX.ofx.bundle"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"

MODE="user"; ACTION="install"; BUNDLE=""; DEST_OVERRIDE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --system)    MODE="system" ;;
    --user)      MODE="user" ;;
    --uninstall) ACTION="uninstall" ;;
    --bundle)    BUNDLE="${2:?--bundle needs a path}"; shift ;;
    --dest)      DEST_OVERRIDE="${2:?--dest needs a directory}"; shift ;;  # testing / custom OFX_PLUGIN_PATH
    -h|--help)   sed -n '2,10p' "$0"; exit 0 ;;
    *) echo "ShutterFX install: unknown option '$1' (try --help)" >&2; exit 2 ;;
  esac
  shift
done

err() { echo "ShutterFX install FAILED: $*" >&2; exit 1; }

if [ "$(uname -s)" != "Darwin" ] && [ -z "${SHUTTERFX_SKIP_OS_CHECK:-}" ] && [ -z "$DEST_OVERRIDE" ]; then
  err "this installer targets macOS. (Use --dest DIR to install elsewhere.)"
fi

if [ -n "$DEST_OVERRIDE" ]; then PLUGINS_DIR="$DEST_OVERRIDE"
elif [ "$MODE" = "system" ]; then PLUGINS_DIR="/Library/OFX/Plugins"
else PLUGINS_DIR="$HOME/Library/OFX/Plugins"; fi
TARGET="$PLUGINS_DIR/$NAME"

# Use sudo only when the destination is not writable by us (system installs).
SUDO=""
needs_sudo() { local d="$1"; while [ ! -e "$d" ]; do d="$(dirname "$d")"; done; [ ! -w "$d" ]; }
if needs_sudo "$PLUGINS_DIR"; then
  [ "$MODE" = "system" ] || err "cannot write to $PLUGINS_DIR"
  SUDO="sudo"; echo "Administrator rights are needed to write to $PLUGINS_DIR."
fi

if [ "$ACTION" = "uninstall" ]; then
  if [ -d "$TARGET" ]; then $SUDO rm -rf "$TARGET" && echo "Removed $TARGET"
  else echo "Nothing to remove: $TARGET does not exist."; fi
  exit 0
fi

# Locate the built bundle.
if [ -z "$BUNDLE" ]; then
  for c in "$ROOT/build/Release/$NAME" "$ROOT/build/Debug/$NAME"; do
    [ -d "$c" ] && { BUNDLE="$c"; break; }
  done
fi
[ -n "$BUNDLE" ] && [ -d "$BUNDLE" ] || err "no built bundle found. Build first (packaging/build_macos.sh) or pass --bundle PATH."
[ "$(basename "$BUNDLE")" = "$NAME" ] || err "bundle must be named $NAME (got $(basename "$BUNDLE"))"
[ -f "$BUNDLE/Contents/Info.plist" ] || err "$BUNDLE/Contents/Info.plist is missing"
ls "$BUNDLE"/Contents/*/ShutterFX.ofx >/dev/null 2>&1 || err "no Contents/<arch>/ShutterFX.ofx binary in $BUNDLE"

echo "Installing $BUNDLE"
echo "        -> $TARGET"
$SUDO mkdir -p "$PLUGINS_DIR" || err "could not create $PLUGINS_DIR"

# Copy to a temporary name first so a failed copy never leaves a half-installed plugin.
TMP="$PLUGINS_DIR/.ShutterFX.ofx.bundle.installing.$$"
$SUDO rm -rf "$TMP"
if command -v ditto >/dev/null 2>&1; then $SUDO ditto "$BUNDLE" "$TMP" || err "copy failed"
else $SUDO cp -R "$BUNDLE" "$TMP" || err "copy failed"; fi
# Files built locally are not quarantined, but a bundle that came through a download/zip is.
if command -v xattr >/dev/null 2>&1; then $SUDO xattr -dr com.apple.quarantine "$TMP" 2>/dev/null || true; fi
$SUDO rm -rf "$TARGET"
$SUDO mv "$TMP" "$TARGET" || err "could not move the bundle into place"

[ -f "$TARGET/Contents/Info.plist" ] || err "installed bundle is incomplete"
echo
echo "SUCCESS: ShutterFX installed at $TARGET"
echo
echo "Next: quit DaVinci Resolve completely (Cmd+Q) and start it again."
echo "Then: Color or Edit page > Effects > OpenFX (Effects Library) > ShutterFX."
if [ "$MODE" = "user" ] && [ -z "$DEST_OVERRIDE" ]; then
  echo
  echo "If ShutterFX does not appear: the OFX standard only guarantees /Library/OFX/Plugins."
  echo "Re-run with --system to install there:  packaging/install.sh --system"
fi
