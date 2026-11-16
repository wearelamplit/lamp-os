#!/bin/bash
# Runs a command under one machine-wide lock so ESP32 firmware builds queue.
# The lamp build (custom_sdkconfig) rewrites the shared ~/.platformio framework libs and a
# wisp build reinstalls them, so concurrent builds can link a half-written .a. Usage: with_pio_lock.sh <cmd> [args...]
lock="${LAMP_PIO_LOCK:-$HOME/.platformio/lamp-build.lock}"
mkdir -p "$(dirname "$lock")"
if command -v flock >/dev/null; then
  try=(flock -n -o "$lock" true); hold=(flock -o "$lock")
else
  try=(lockf -s -k -t 0 "$lock" true); hold=(lockf -k "$lock")
fi
"${try[@]}" || echo "waiting for another firmware build to finish (lock: $lock)" >&2
exec "${hold[@]}" "$@"
