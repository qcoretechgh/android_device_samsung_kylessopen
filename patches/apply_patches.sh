#!/bin/bash
set -e
TOP_DIR="$(cd "$(dirname "$0")/../../../.." && pwd)"
PATCH_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "Applying CM7.2 kylessopen patches to $TOP_DIR ..."
git -C "$TOP_DIR/system/bluetooth" apply "$PATCH_DIR/0001-bluedroid-fix-re-enable.patch"
git -C "$TOP_DIR/system/vold" apply "$PATCH_DIR/0002-vold-ignore-empty-loop-devices.patch"
git -C "$TOP_DIR/system/core" apply "$PATCH_DIR/0003-core-libnetutils-adb-ueventd.patch"
echo "All patches applied successfully!"
