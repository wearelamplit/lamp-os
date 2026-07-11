#!/usr/bin/env bash
# Download the latest signed firmware from a GitHub Release and flash it.
#
# Usage: VARIANT=standard PORT=/dev/cu.usbserial-0 RELEASE_TAG=beta scripts/flash_signed_release.sh
#
# Env vars:
#   VARIANT      lamp variant — standard (default) or snafu
#   PORT         esptool upload port (required if multiple boards attached)
#   RELEASE_TAG  GitHub release tag to pull from — beta (default) or stable
#
# This script is for contributors WITHOUT the signing key. It never builds or
# signs; it only downloads a prebuilt signed binary and writes it to the lamp.
#
# Requires: gh (GitHub CLI, authenticated), esptool (pip install esptool)
# App partition offset: 0x10000 (see software/lamp-os/partitions.csv)

set -euo pipefail

VARIANT="${VARIANT:-standard}"
RELEASE_TAG="${RELEASE_TAG:-beta}"
ASSET="lamp-firmware-${VARIANT}-signed.bin"
TMPFILE="$(mktemp -t lamp-signed-XXXX.bin)"

cleanup() { rm -f "$TMPFILE"; }
trap cleanup EXIT

# Fail early if gh isn't authed.
if ! gh auth status --active > /dev/null 2>&1; then
  echo "ERROR: gh CLI is not authenticated. Run: gh auth login" >&2
  exit 1
fi

REPO="$(gh repo view --json nameWithOwner -q .nameWithOwner 2>/dev/null || true)"
if [ -z "$REPO" ]; then
  echo "ERROR: could not determine repo. Run from inside the lamp-os repo." >&2
  exit 1
fi

echo "[flash:signed] downloading ${ASSET} from release '${RELEASE_TAG}' (${REPO})"
if ! gh release download "${RELEASE_TAG}" \
      --repo "${REPO}" \
      --pattern "${ASSET}" \
      --output "${TMPFILE}" \
      --clobber 2>&1; then
  echo "ERROR: asset '${ASSET}' not found in release '${RELEASE_TAG}'." >&2
  echo "       Run 'Release (beta)' workflow manually on GitHub, then retry." >&2
  exit 1
fi

# Verify the download is non-empty.
if [ ! -s "$TMPFILE" ]; then
  echo "ERROR: downloaded file is empty." >&2
  exit 1
fi

echo "[flash:signed] flashing ${ASSET} to app partition (0x10000)…"
PORT_ARG="${PORT:+--port ${PORT}}"
# ponytail: flash only the app partition; bootloader/partitions/spiffs stay untouched for OTA-style writes.
esptool ${PORT_ARG:-} --chip esp32 --baud 921600 write_flash 0x10000 "$TMPFILE"

echo "[flash:signed] done — lamp should reboot into ${VARIANT}/${RELEASE_TAG} firmware."
