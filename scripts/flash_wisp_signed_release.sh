#!/usr/bin/env bash
# Download the signed wisp release image and UPDATE a wisp over USB: writes only
# app0 (the signed firmware). NVS (name/config), otadata, the bootloader, and
# the partition table are never written, so they persist across the update.
#
# The wisp does not self-OTA; it always boots app0, so a plain app0 write is the
# whole update. A truly blank wisp (no valid bootloader/partitions) needs a full
# flash via pio, not this script.
#
# Usage: PORT=/dev/cu.usbmodem143401 RELEASE_TAG=beta scripts/flash_wisp_signed_release.sh
#
# Env vars:
#   PORT         esptool upload port (required if multiple boards attached)
#   RELEASE_TAG  GitHub release tag to pull from: beta (default) or stable
#
# For people WITHOUT the signing key: downloads a prebuilt image, never builds
# or signs. Requires: curl, esptool (pip install esptool).

set -euo pipefail

RELEASE_TAG="${RELEASE_TAG:-beta}"
ASSET="wisp-firmware-signed.bin"
REPO="wearelamplit/lamp-os"
URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/${ASSET}"
APP0_OFFSET="0x10000"
APP0_SIZE=$((0x1E0000))
TMPFILE="$(mktemp -t wisp-fw-XXXX.bin)"
# Prefer the esptool binary on PATH, else the module form (pip installs the
# module but not always the console script). Array form so a binary path with a
# space in it survives.
if command -v esptool >/dev/null 2>&1; then
  ESPTOOL=( "$(command -v esptool)" )
else
  ESPTOOL=( python3 -m esptool )
fi

cleanup() { rm -f "$TMPFILE"; }
trap cleanup EXIT

echo "[wisp:flash:release] downloading ${ASSET} from release '${RELEASE_TAG}' (${REPO})"
if ! curl -fSL --retry 3 -o "${TMPFILE}" "${URL}"; then
  echo "ERROR: asset '${ASSET}' not found in release '${RELEASE_TAG}'." >&2
  echo "       Publish a release for tag '${RELEASE_TAG}' on GitHub, then retry." >&2
  exit 1
fi

if [ ! -s "$TMPFILE" ]; then
  echo "ERROR: downloaded file is empty." >&2
  exit 1
fi

# A larger image would overrun app0 into app1.
IMG_SIZE="$(wc -c < "$TMPFILE")"
if [ "$IMG_SIZE" -gt "$APP0_SIZE" ]; then
  echo "ERROR: image is ${IMG_SIZE} bytes, larger than app0 (${APP0_SIZE})." >&2
  exit 1
fi

echo "[wisp:flash:release] updating firmware (app0 @${APP0_OFFSET}); NVS/config preserved..."
PORT_ARG=()
[ -n "${PORT:-}" ] && PORT_ARG=(--port "$PORT")
"${ESPTOOL[@]}" ${PORT_ARG[@]+"${PORT_ARG[@]}"} --chip esp32c6 --baud 921600 write_flash \
  "${APP0_OFFSET}" "$TMPFILE"

echo "[wisp:flash:release] done — wisp updated to ${RELEASE_TAG}, name/config kept."
