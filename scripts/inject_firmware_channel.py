#!/usr/bin/env python3
"""PIO pre-build hook: inject FIRMWARE_CHANNEL build define from the env's
`custom_lamp_variant` option combined with the LAMP_FIRMWARE_CHANNEL env
var.

The channel slot in the LSIG footer and on every MSG_FW_OFFER wire frame
carries `{lampType}-{channel}` (e.g. "standard-stable", "snafu-beta") so
that the existing silent-drop on channel-mismatch enforces per-variant
OTA gating for free.

This hook derives the variant from the per-env `custom_lamp_variant`
project option and combines it with `LAMP_FIRMWARE_CHANNEL` (or "dev"
default). Result is injected as -D FIRMWARE_CHANNEL='"<combined>"'.

When the env doesn't declare `custom_lamp_variant` (e.g. the native test
env), the raw channel is injected without a prefix — same shape as
before so any cross-env build paths keep working.

Shared build hook (repo-root scripts/), wired from each component's
platformio.ini as:
  extra_scripts =
    pre:../../scripts/inject_firmware_channel.py
The wisp has no `custom_lamp_variant`, so the channel injects without a
prefix and only LAMP_DEBUG matters there.
"""

import os
import sys

Import("env")  # SCons / PIO global

LSIG_CHANNEL_LEN = 16

# Default to "dev" when unset: a local build is a dev build (unsigned,
# LAMP_DEBUG on, OTA island). CI passes an explicit beta/stable channel.
# This default MUST match sign_firmware.py's DEFAULT_CHANNEL.
base = os.environ.get("LAMP_FIRMWARE_CHANNEL", "dev").strip()
if not base:
    base = "dev"

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

# Escaped quotes so the value reaches GCC as a string literal, dodging
# shell-quoting hazards in the CPPDEFINES tuple form.
env.Append(CPPDEFINES=[("FIRMWARE_CHANNEL", '\\"' + combined + '\\"')])
print(f"[inject_firmware_channel] FIRMWARE_CHANNEL={combined}")

# LAMP_DEBUG gates bench-only affordances (testGreet, extra serial logging).
# Only a dev build keeps them; beta and stable ship quiet. Keyed off the raw
# channel, not the variant-prefixed slot.
debug_on = base == "dev"
if debug_on:
    env.Append(CPPDEFINES=["LAMP_DEBUG"])
print(f"[inject_firmware_channel] LAMP_DEBUG={'on' if debug_on else 'off'}")
