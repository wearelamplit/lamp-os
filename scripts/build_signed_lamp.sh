#!/usr/bin/env bash
# Build a signed lamp firmware (beta/stable channel appends the ed25519 LSIG
# footer via sign_firmware.py) and optionally flash the signed image over USB.
# The dev channel is unsigned and OTA-island, so it cannot source OTA; a signed
# build needs beta or stable plus the private key at ~/.lamp-os-firmware-key.bin.
#
# Build-only (workflow / artifact): produces .pio/build/<env>/firmware-signed.bin
# Flash (bench): writes the signed image to app0 @0x10000 and clears otadata so
# the bootloader runs the freshly-flashed slot. NVS (name/colors/adoption at
# 0x9000) is preserved.
#
# Usage:
#   VARIANT=standard CHANNEL=beta scripts/build_signed_lamp.sh                        # build only
#   VARIANT=standard CHANNEL=beta PORT=/dev/cu.SLAB_USBtoUART8 scripts/build_signed_lamp.sh --flash
#   DEBUG=1 ... --flash    # force LAMP_DEBUG on a signed build for bench measurement
#
# Env:
#   VARIANT  standard (default) | snafu
#   CHANNEL  beta (default) | stable          (dev is rejected — it is unsigned)
#   PORT     esptool port (required with --flash)
#   DEBUG    1 to force -D LAMP_DEBUG on a signed build (bench serial logs). A
#            shipping beta/stable is quiet; use only for bench measurement.
set -euo pipefail

VARIANT="${VARIANT:-standard}"
CHANNEL="${CHANNEL:-beta}"
ENV="upesy_wroom_${VARIANT}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LAMP_DIR="$ROOT/software/lamp-os"
SIGNED="$LAMP_DIR/.pio/build/$ENV/firmware-signed.bin"

if [ "$CHANNEL" = "dev" ]; then
  echo "[build_signed] ERROR: CHANNEL=dev is unsigned/OTA-island. Use beta or stable." >&2
  exit 1
fi
if [ ! -f "$HOME/.lamp-os-firmware-key.bin" ]; then
  echo "[build_signed] ERROR: signing key missing at ~/.lamp-os-firmware-key.bin" >&2
  exit 1
fi

EXTRA_FLAGS=""
[ "${DEBUG:-0}" = "1" ] && EXTRA_FLAGS="-D LAMP_DEBUG" && \
  echo "[build_signed] DEBUG=1 — forcing LAMP_DEBUG on a signed build (bench measurement only)."

echo "[build_signed] building $ENV on channel '$CHANNEL' (signed)..."
(
  cd "$LAMP_DIR"
  export LAMP_FIRMWARE_CHANNEL="$CHANNEL"
  [ -n "$EXTRA_FLAGS" ] && export PLATFORMIO_BUILD_FLAGS="$EXTRA_FLAGS"
  pio run -e "$ENV"
)

[ -f "$SIGNED" ] || { echo "[build_signed] ERROR: signed image not produced at $SIGNED" >&2; exit 1; }
echo "[build_signed] signed image ready: $SIGNED ($(wc -c < "$SIGNED") bytes)"

if [ "${1:-}" = "--flash" ]; then
  [ -n "${PORT:-}" ] || { echo "[build_signed] ERROR: --flash needs PORT=..." >&2; exit 1; }
  if command -v esptool >/dev/null 2>&1; then ESPTOOL=( "$(command -v esptool)" ); else ESPTOOL=( python3 -m esptool ); fi
  echo "[build_signed] flashing signed app0 @0x10000 to $PORT (NVS preserved, otadata cleared to boot app0)..."
  "${ESPTOOL[@]}" --port "$PORT" --chip esp32 --baud 921600 write_flash 0x10000 "$SIGNED"
  "${ESPTOOL[@]}" --port "$PORT" --chip esp32 --baud 921600 erase_region 0xE000 0x2000
  echo "[build_signed] done — $VARIANT/$CHANNEL flashed to $PORT."
fi
