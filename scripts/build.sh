#!/usr/bin/env bash
# Builds the cameralink server. Run on the Pi (or any Linux ARM/x86 host)
# with Sony's CrSDK already unpacked into third_party/CrSDK -- see
# docs/BUILDING.md for how to obtain and place it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_DIR="$REPO_ROOT/third_party/CrSDK"
OUT_DIR="$REPO_ROOT/server/build"

if [ ! -d "$SDK_DIR/CRSDK" ] || [ ! -f "$SDK_DIR/libCr_Core.so" ]; then
  echo "error: Sony CrSDK not found at $SDK_DIR" >&2
  echo "See docs/BUILDING.md for how to obtain and place it." >&2
  exit 1
fi

if [ ! -f "$SDK_DIR/CrDebugString.cpp" ] || [ ! -f "$SDK_DIR/CrDebugString.h" ]; then
  echo "error: CrDebugString.cpp/.h not found at $SDK_DIR" >&2
  echo "See docs/BUILDING.md for how to obtain and place them." >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

# -fsigned-char is required on ARM (char defaults to unsigned there, but the
# SDK headers assume signed -- without this flag the build fails with
# "enumerator value is outside the range of underlying type" errors).
#
# CrDebugString.cpp is Sony's own sample-code property-name lookup table
# (CrDevicePropertyString()) -- used by the /api/debug/allprops endpoint to
# show human-readable names instead of raw hex codes.
g++ -std=c++17 -pthread -fsigned-char -fstack-protector-all \
  -I "$SDK_DIR/CRSDK" -I "$REPO_ROOT/third_party" -I "$SDK_DIR" \
  "$REPO_ROOT/server/main.cpp" "$SDK_DIR/CrDebugString.cpp" \
  -L "$SDK_DIR" -lCr_Core \
  -Wl,-rpath,'$ORIGIN' \
  -o "$OUT_DIR/cameralink_server"

# The SDK's shared libraries must sit as direct siblings of the binary's
# working directory (not just alongside libCr_Core.so) -- libCr_Core.so
# resolves the CrAdapter/ plugin folder relative to the process's cwd, not
# its own file location. See docs/ARCHITECTURE.md for the story behind
# this gotcha.
cp "$SDK_DIR/"*.so "$OUT_DIR/"
cp -r "$SDK_DIR/CrAdapter" "$OUT_DIR/CrAdapter"
cp -r "$REPO_ROOT/server/public" "$OUT_DIR/public"

echo "Built: $OUT_DIR/cameralink_server"
echo "Run from that directory: cd $OUT_DIR && ./cameralink_server"
