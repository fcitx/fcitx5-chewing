#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
FCITX_LIB_DIR="${FCITX_LIB_DIR:-/usr/lib/fcitx5}"
ICON_DIR="${ICON_DIR:-/usr/share/icons/hicolor/scalable/apps}"
LIB_NAME="libchewing.so"
RUN_TESTS=1
RESTART_FCITX="${RESTART_FCITX:-1}"

usage() {
  cat <<EOF
Usage: $0 [options]

Build, test, deploy local fcitx5-chewing, install smart-mode icons, and restart fcitx5.

Options:
  --no-test          Skip ctest.
  --no-restart       Do not restart fcitx5.
  --build-dir DIR    Build directory. Default: ./build
  --lib-dir DIR      Fcitx addon dir. Default: /usr/lib/fcitx5
  -h, --help         Show this help.

Environment:
  BUILD_DIR          Same as --build-dir.
  FCITX_LIB_DIR      Same as --lib-dir.
  RESTART_FCITX=0    Same as --no-restart.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-test)
      RUN_TESTS=0
      shift
      ;;
    --no-restart)
      RESTART_FCITX=0
      shift
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --lib-dir)
      FCITX_LIB_DIR="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -d "$BUILD_DIR" ]]; then
  echo "Build directory not found: $BUILD_DIR" >&2
  echo "Run cmake configure first, e.g.: cmake -B build -S ." >&2
  exit 1
fi

echo "==> Building fcitx5-chewing"
cmake --build "$BUILD_DIR" -j"$(nproc)"

if [[ "$RUN_TESTS" == "1" ]]; then
  echo "==> Running tests"
  ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

BUILT_LIB="$BUILD_DIR/src/$LIB_NAME"
if [[ ! -f "$BUILT_LIB" ]]; then
  echo "Built library not found: $BUILT_LIB" >&2
  exit 1
fi

echo "==> Installing addon module"
echo "    $BUILT_LIB -> $FCITX_LIB_DIR/$LIB_NAME"
sudo install -Dm755 "$BUILT_LIB" "$FCITX_LIB_DIR/$LIB_NAME"

if [[ -d "$ROOT_DIR/data/scalable/apps" ]]; then
  echo "==> Installing mode icons"
  for icon in fcitx-chewing-smart.svg fcitx-chewing-chinese.svg fcitx-chewing-english.svg; do
    if [[ -f "$ROOT_DIR/data/scalable/apps/$icon" ]]; then
      sudo install -Dm644 "$ROOT_DIR/data/scalable/apps/$icon" "$ICON_DIR/$icon"
    fi
  done
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    sudo gtk-update-icon-cache -f /usr/share/icons/hicolor >/dev/null || true
  fi
fi

echo "==> Verifying installed module"
stat "$FCITX_LIB_DIR/$LIB_NAME"
if strings "$FCITX_LIB_DIR/$LIB_NAME" | grep -q 'chewing-input-mode'; then
  echo "OK: installed module contains smart input mode support."
else
  echo "WARNING: installed module does not contain chewing-input-mode marker." >&2
fi

if [[ "$RESTART_FCITX" == "1" ]]; then
  echo "==> Restarting fcitx5"
  pkill fcitx5 2>/dev/null || true
  fcitx5 -d
fi

echo "Done. Switch to Chewing and test the status icon / smart input mode."
