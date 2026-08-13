#!/usr/bin/env python3
"""PIO pre-build hook: inject LAMP_COMMAND_KEY_HEX so a signed build bakes in
the shared key that authenticates MSG_EVENT / MSG_COMMAND frames.

Key injection tracks the channel: a dev build injects nothing, so unsigned
firmware runs keyless (command_auth is permissive). A beta or stable build
resolves the 64-char hex key from LAMP_COMMAND_KEY_HEX or
~/.lamp-os-command-key.hex and injects it as -D LAMP_COMMAND_KEY_HEX='"<hex>"'.
A beta/stable build with no/invalid key fails the build; keyless signed
firmware would accept every MSG_COMMAND / MSG_EVENT unauthenticated.

Wired from software/lamp-os/platformio.ini as:
  extra_scripts =
    pre:../../scripts/inject_command_key.py
"""

import os
from pathlib import Path

Import("env")  # SCons / PIO global

KEY_PATH = Path.home() / ".lamp-os-command-key.hex"
KEY_LEN_HEX = 64


def _is_hex(s):
    if len(s) != KEY_LEN_HEX:
        return False
    try:
        bytes.fromhex(s)
    except ValueError:
        return False
    return True


# Must match inject_firmware_channel.py's default.
channel = (os.environ.get("LAMP_FIRMWARE_CHANNEL") or "dev").strip() or "dev"
if channel == "dev":
    print("[inject_command_key] skipped (dev build); firmware runs keyless")
else:
    key = os.environ.get("LAMP_COMMAND_KEY_HEX")
    if key:
        key = key.strip()
    elif KEY_PATH.exists():
        key = KEY_PATH.read_text().strip()

    if key and _is_hex(key):
        env.Append(CPPDEFINES=[("LAMP_COMMAND_KEY_HEX", '\\"' + key + '\\"')])
        print(f"[inject_command_key] LAMP_COMMAND_KEY_HEX={key[:6]}…")
    else:
        print("[inject_command_key] ERROR: signed build but no valid "
              "64-char hex key (LAMP_COMMAND_KEY_HEX / ~/.lamp-os-command-key.hex); "
              "refusing to build keyless firmware")
        env.Exit(1)
