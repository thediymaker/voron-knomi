#!/bin/bash
# Merge bootloader, partition table, boot selector and firmware into one image
# for a first install over USB (flashed at offset 0). Run after `pio run`.
# Usage: scripts/merge_factory.sh <output.bin>
set -euo pipefail
OUT=${1:-dist/knomi-ng-factory.bin}
BUILD=.pio/build/knomiv2
PKG=${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages
mkdir -p "$(dirname "$OUT")"
python3 "$PKG/tool-esptoolpy/esptool.py" --chip esp32s3 merge_bin -o "$OUT" \
  --flash_mode keep --flash_freq keep --flash_size keep \
  0x0 "$BUILD/bootloader.bin" \
  0x8000 "$BUILD/partitions.bin" \
  0xe000 "$PKG/framework-arduinoespressif32/tools/partitions/boot_app0.bin" \
  0x10000 "$BUILD/firmware.bin"
