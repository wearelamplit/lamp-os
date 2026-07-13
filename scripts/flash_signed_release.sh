#!/usr/bin/env bash
# Download a full release image and flash a lamp over USB. This is the console
# equivalent of the web installer at update.lamplit.ca: it writes the WHOLE
# image (bootloader + partitions + firmware + spiffs) at offset 0, so the web
# config UI ships with it. A real flash does not assume the old SPIFFS carries
# over. NVS (name/config) is not in the image, so it persists across the write.
#
# Usage: VARIANT=standard PORT=/dev/cu.usbserial-0 RELEASE_TAG=beta scripts/flash_signed_release.sh
#
# Env vars:
#   VARIANT      lamp variant: standard (default) or snafu
#   PORT         esptool upload port (required if multiple boards attached)
#   RELEASE_TAG  GitHub release tag to pull from: beta (default) or stable
#
# For people WITHOUT the signing key: downloads a prebuilt image, never builds
# or signs. Requires: curl, esptool (pip install esptool).

set -euo pipefail

VARIANT="${VARIANT:-standard}"
RELEASE_TAG="${RELEASE_TAG:-beta}"
ASSET="distribution-${VARIANT}.bin"
REPO="wearelamplit/lamp-os"
URL="https://github.com/${REPO}/releases/download/${RELEASE_TAG}/${ASSET}"
TMPFILE="$(mktemp -t lamp-dist-XXXX.bin)"
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

echo "[flash:release] flashing full image (bootloader+partitions+firmware+spiffs) at 0x0..."
PORT_ARG=()
[ -n "${PORT:-}" ] && PORT_ARG=(--port "$PORT")
"${ESPTOOL[@]}" ${PORT_ARG[@]+"${PORT_ARG[@]}"} --chip esp32 --baud 921600 write_flash 0x0 "$TMPFILE"

echo "[flash:release] done — lamp should reboot into ${VARIANT}/${RELEASE_TAG} firmware."
