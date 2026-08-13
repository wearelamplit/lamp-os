#!/usr/bin/env bash
# Download a release image and UPDATE a lamp over USB: flashes only app0
# (firmware) + spiffs (web config UI), carved out of the merged image. NVS
# (name/colors/adoption) is never written, so it persists across the update.
# The merged image is 0xFF across the NVS region, so a whole-image 0x0 write
# would erase name/colors/adoption; this is not that.
#
# otadata IS reset here. Mesh OTA is A/B: it writes the inactive app slot and
# flips otadata (app0<->app1). This script only writes app0, so a lamp that
# last OTA-booted app1 would keep booting app1 and never see the new app0. The
# erase forces the bootloader back to ota_0. otadata (@0xE000) is separate from
# nvs (@0x9000), so name/config survive.
#
# This is an update path, not a full/erase flash. A truly blank lamp (no valid
# bootloader/partitions) needs the web installer at update.lamplit.ca or a
# :signed build, not this script.
#
# Usage: VARIANT=standard PORT=/dev/cu.usbserial-0 RELEASE_TAG=beta scripts/flash_signed_release.sh
#
# Env vars:
#   VARIANT      lamp variant: standard (default) or snafu
#   PORT         esptool upload port (required if multiple boards attached)
#   RELEASE_TAG  GitHub release tag to pull from: beta (default) or stable
#
# For people WITHOUT the signing key: downloads a prebuilt image, never builds
# or signs. Requires: curl, esptool (pip install esptool), dd.

set -euo pipefail

VARIANT="${VARIANT:-standard}"
RELEASE_TAG="${RELEASE_TAG:-beta}"
ASSET="distribution-${VARIANT}.bin"
REPO="wearelamplit/lamp-os"
URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/${ASSET}"
TMPFILE="$(mktemp -t lamp-dist-XXXX.bin)"
APP0FILE="$(mktemp -t lamp-app0-XXXX.bin)"
SPIFFSFILE="$(mktemp -t lamp-spiffs-XXXX.bin)"
# Prefer the esptool binary on PATH, else the module form (pip installs the
# module but not always the console script). Array form so a binary path with a
# space in it survives.
if command -v esptool >/dev/null 2>&1; then
  ESPTOOL=( "$(command -v esptool)" )
else
  ESPTOOL=( python3 -m esptool )
fi

cleanup() { rm -f "$TMPFILE" "$APP0FILE" "$SPIFFSFILE"; }
trap cleanup EXIT

# otadata offset+size from the partition table, not hardcoded, so a repartition
# can't silently desync this erase.
PARTITIONS_CSV="$(cd "$(dirname "${BASH_SOURCE[0]}")/../software/lamp-os" && pwd)/partitions.csv"
read -r OTADATA_OFF OTADATA_SIZE < <(
  awk -F',' '{gsub(/[ \t]/,"")} $3=="ota" && $2=="data" { print $4, $5 }' "$PARTITIONS_CSV"
)
if [ -z "${OTADATA_OFF:-}" ] || [ -z "${OTADATA_SIZE:-}" ]; then
  echo "ERROR: could not derive otadata offset/size from ${PARTITIONS_CSV}." >&2
  exit 1
fi

echo "[flash:release] downloading ${ASSET} from release '${RELEASE_TAG}' (${REPO})"
if ! curl -fSL --retry 3 -o "${TMPFILE}" "${URL}"; then
  echo "ERROR: asset '${ASSET}' not found in release '${RELEASE_TAG}'." >&2
  echo "       Publish a release for tag '${RELEASE_TAG}' on GitHub, then retry." >&2
  exit 1
fi

if [ ! -s "$TMPFILE" ]; then
  echo "ERROR: downloaded file is empty." >&2
  exit 1
fi

# A short image would dd garbage past its end into the flashed regions.
IMG_SIZE="$(wc -c < "$TMPFILE")"
if [ "$IMG_SIZE" -lt $((0x400000)) ]; then
  echo "ERROR: image is ${IMG_SIZE} bytes, expected at least 4 MiB (0x400000)." >&2
  exit 1
fi

# Carve app0 (0x10000, len 0x1e0000) and spiffs (0x3d0000, len 0x30000) out of
# the merged image, in 4096-byte blocks.
dd if="$TMPFILE" of="$APP0FILE"   bs=4096 skip=16  count=480 2>/dev/null
dd if="$TMPFILE" of="$SPIFFSFILE" bs=4096 skip=976 count=48  2>/dev/null

echo "[flash:release] updating firmware (app0 @0x10000) + spiffs (@0x3d0000); NVS preserved..."
PORT_ARG=()
[ -n "${PORT:-}" ] && PORT_ARG=(--port "$PORT")
"${ESPTOOL[@]}" ${PORT_ARG[@]+"${PORT_ARG[@]}"} --chip esp32 --baud 921600 write_flash \
  0x10000 "$APP0FILE" 0x3d0000 "$SPIFFSFILE"

echo "[flash:release] resetting otadata (${OTADATA_OFF}+${OTADATA_SIZE}) so the bootloader boots ota_0 (app0)..."
"${ESPTOOL[@]}" ${PORT_ARG[@]+"${PORT_ARG[@]}"} --chip esp32 erase_region "$OTADATA_OFF" "$OTADATA_SIZE"

echo "[flash:release] done — lamp updated to ${VARIANT}/${RELEASE_TAG}, name/colors/adoption kept."
