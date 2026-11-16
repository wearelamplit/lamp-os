#!/bin/bash
# Runs a command under one machine-wide lock so lamp firmware builds queue.
# custom_sdkconfig rewrites the shared ~/.platformio framework libs, so two
# concurrent builds can link a half-written .a. Usage: with_pio_lock.sh <cmd> [args...]
lock="${LAMP_PIO_LOCK:-$HOME/.platformio/lamp-build.lock}"
mkdir -p "$(dirname "$lock")"
if command -v flock >/dev/null; then
  try=(flock -n "$lock" true); hold=(flock "$lock")
else
  try=(lockf -t 0 "$lock" true); hold=(lockf "$lock")
fi
"${try[@]}" || echo "waiting for another lamp firmware build to finish (lock: $lock)" >&2
exec "${hold[@]}" "$@"
