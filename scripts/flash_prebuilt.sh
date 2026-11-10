#!/usr/bin/env bash
# Interactively UPDATE a lamp over USB from a locally-prebuilt distribution
# image. Picks an image from the cache dir and a USB port, then delegates the
# carve/erase/flash to flash_signed_release.sh via IMAGE=. NVS (name/config)
# is preserved; see flash_signed_release.sh for the update-path details.
#
# Usage: scripts/flash_prebuilt.sh
#
# Env vars:
#   CACHE_DIR  where prebuilt distribution-<variant>*.bin live (default
#              ~/.cache/lamp-os)
#   IMAGE      flash this image, skip the picker
#   PORT       esptool upload port, skip the picker

set -euo pipefail

CACHE_DIR="${CACHE_DIR:-$HOME/.cache/lamp-os}"

pick() {
  local prompt="$1"; shift
  local options=("$@")
  local i
  for i in "${!options[@]}"; do
    printf '  %d) %s\n' "$((i + 1))" "${options[$i]}" >&2
  done
  local choice
  read -r -p "$prompt " choice
  if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt "${#options[@]}" ]; then
    echo "ERROR: invalid selection '${choice}'." >&2
    exit 1
  fi
  printf '%s\n' "${options[$((choice - 1))]}"
}

if [ -z "${IMAGE:-}" ]; then
  IMAGES=()
  while IFS= read -r f; do IMAGES+=("$f"); done < <(
    find "$CACHE_DIR" -maxdepth 1 -name 'distribution-*.bin' 2>/dev/null | sort
  )
  if [ "${#IMAGES[@]}" -eq 0 ]; then
    echo "ERROR: no prebuilt images (distribution-*.bin) in ${CACHE_DIR}." >&2
    echo "       Build a variant image there first (release downloads are cached as release-*.bin and are not listed)." >&2
    exit 1
  fi
  echo "Prebuilt images in ${CACHE_DIR}:" >&2
  IMAGE="$(pick "Image #:" "${IMAGES[@]}")"
fi

if [ -z "${PORT:-}" ]; then
  PORTS=()
  for p in /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART*; do
    [ -e "$p" ] && PORTS+=("$p")
  done
  if [ "${#PORTS[@]}" -eq 0 ]; then
    echo "ERROR: no USB serial ports (/dev/cu.usbserial-*, /dev/cu.SLAB_USBtoUART*) found." >&2
    exit 1
  fi
  echo "USB serial ports:" >&2
  PORT="$(pick "Port #:" "${PORTS[@]}")"
fi

# distribution-<variant>.bin or distribution-<variant>-<channel>.bin -> <variant>.
base="$(basename "$IMAGE")"
base="${base#distribution-}"
VARIANT="${base%%[-.]*}"

echo "[flash:prebuilt] ${base} -> ${PORT} (variant ${VARIANT})"
IMAGE="$IMAGE" PORT="$PORT" VARIANT="$VARIANT" \
  bash "$(dirname "${BASH_SOURCE[0]}")/flash_signed_release.sh"
