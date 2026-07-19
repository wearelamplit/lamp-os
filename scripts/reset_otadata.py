#!/usr/bin/env python3
"""PIO post-upload hook: erase the otadata partition after a USB upload.

`pio run -t upload` writes app0 but never otadata. Mesh OTA is A/B: it writes
the inactive app slot and flips otadata (app0<->app1). So a lamp that last
OTA-booted app1 keeps booting app1 after a USB flash of app0 — the flash
"succeeds" with zero visible effect. Erasing otadata forces the bootloader
back to ota_0 (app0). otadata is separate from nvs, so name/config survive.

Runs only on upload (AddPostAction on the upload node), so a plain `pio run`
build is untouched. Offset+size come from partitions.csv, the same source the
firmware boots from.

Wired from software/lamp-os/platformio.ini as:
  extra_scripts = post:../../scripts/reset_otadata.py
"""

import subprocess
import sys
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # injected by SCons/PIO


def _otadata_region(project_dir: Path) -> tuple[str, str]:
    csv = project_dir / "partitions.csv"
    for line in csv.read_text().splitlines():
        cols = [c.strip() for c in line.split(",")]
        if len(cols) >= 5 and cols[1] == "data" and cols[2] == "ota":
            return cols[3], cols[4]
    sys.exit(f"[reset_otadata] FATAL: no otadata partition in {csv}")


def _post_upload(source, target, env):  # noqa: ARG001  (SCons signature)
    off, size = _otadata_region(Path(env.subst("$PROJECT_DIR")).resolve())
    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32"]
    port = env.subst("$UPLOAD_PORT")
    if port:
        cmd += ["--port", port]
    cmd += ["erase_region", off, size]
    print(f"[reset_otadata] resetting otadata ({off}+{size}) -> boots ota_0")
    subprocess.run(cmd, check=True)


env.AddPostAction("upload", _post_upload)  # type: ignore[name-defined]
