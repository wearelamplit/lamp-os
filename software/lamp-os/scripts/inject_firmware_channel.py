#!/usr/bin/env python3
"""PIO pre-build hook: inject FIRMWARE_CHANNEL build define from the env's
`custom_lamp_variant` option combined with the LAMP_FIRMWARE_CHANNEL env
var.

The channel slot in the LSIG footer and on every MSG_FW_OFFER wire frame
carries `{lampType}-{channel}` (e.g. "standard-stable", "snafu-beta") so
that the existing silent-drop on channel-mismatch enforces per-variant
OTA gating for free.

This hook derives the variant from the per-env `custom_lamp_variant`
project option and combines it with `LAMP_FIRMWARE_CHANNEL` (or "stable"
default). Result is injected as -D FIRMWARE_CHANNEL='"<combined>"'.

When the env doesn't declare `custom_lamp_variant` (e.g. the native test
env), the raw channel is injected without a prefix — same shape as
before so any cross-env build paths keep working.

Wired from software/lamp-os/platformio.ini as:
  extra_scripts =
    pre:scripts/inject_firmware_channel.py
"""

import os
import sys

Import("env")  # SCons / PIO global

LSIG_CHANNEL_LEN = 16

base = os.environ.get("LAMP_FIRMWARE_CHANNEL", "stable").strip()
if not base:
    base = "stable"

try:
    variant = env.GetProjectOption("custom_lamp_variant")
except Exception:
    variant = None
if variant:
    variant = str(variant).strip()

combined = f"{variant}-{base}" if variant else base

if len(combined.encode("ascii", errors="strict")) > LSIG_CHANNEL_LEN:
    sys.exit(
        f"[inject_firmware_channel] FATAL: combined channel {combined!r} "
        f"exceeds {LSIG_CHANNEL_LEN} bytes"
    )

# Pass the value with escaped quotes so it reaches GCC as a string
# literal (matches the inject_initial_type.py pattern).
env.Append(CPPDEFINES=[("FIRMWARE_CHANNEL", '\\"' + combined + '\\"')])
print(f"[inject_firmware_channel] FIRMWARE_CHANNEL={combined}")
