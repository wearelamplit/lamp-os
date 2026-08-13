#!/usr/bin/env python3
"""PIO pre-build hook: inject the firmware version from the root VERSION file.

Single version source for both lamp and wisp firmware. The root VERSION file
holds one `MAJOR.MINOR.PATCH` line; this hook parses it and injects
`-D LAMP_FW_MAJOR/MINOR/PATCH`, which both version.hpp files derive
FIRMWARE_VERSION from. Bumping a release = edit one file.

Wired from each component's platformio.ini as:
  extra_scripts =
    pre:../../scripts/inject_version.py
"""

import sys
from pathlib import Path

Import("env")  # SCons / PIO global

# `__file__` isn't defined when SCons execs this script, so derive the repo
# root from $PROJECT_DIR (software/<component>; its parent's parent is repo).
VERSION_PATH = Path(env.subst("$PROJECT_DIR")).resolve().parent.parent / "VERSION"

if not VERSION_PATH.exists():
    sys.exit(f"[inject_version] FATAL: VERSION file not found at {VERSION_PATH}")

raw = VERSION_PATH.read_text().strip()
parts = raw.split(".")
if len(parts) != 3 or not all(p.isdigit() for p in parts):
    sys.exit(
        f"[inject_version] FATAL: malformed VERSION {raw!r} in {VERSION_PATH} "
        f"(expected MAJOR.MINOR.PATCH)"
    )

major, minor, patch = (int(p) for p in parts)
env.Append(CPPDEFINES=[
    ("LAMP_FW_MAJOR", major),
    ("LAMP_FW_MINOR", minor),
    ("LAMP_FW_PATCH", patch),
])
print(f"[inject_version] LAMP_FW={major}.{minor}.{patch}")
