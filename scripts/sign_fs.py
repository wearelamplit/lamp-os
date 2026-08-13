#!/usr/bin/env python3
"""PIO pre-buildfs hook: write data/fw.lsig before mkspiffs packs the image.

Wired from software/lamp-os/platformio.ini alongside sign_firmware.py. PIO
passes the build env via Import("env"); this registers a PreAction on the
spiffs.bin target so fw.lsig lands in data/ before mkspiffs globs the dir.

What it signs (must match fs_signature.cpp::computeFsManifestDigest exactly):
  sort data files (excluding fw.lsig) by name, bytewise ascending; then
  SHA-256 over, per file:  u32LE(nameLen) ∥ name ∥ u32LE(contentLen) ∥ content
The ed25519 signature is over that 32-byte digest (hash-then-sign), matching
the firmware signing shape.

fw.lsig layout (72 bytes), see fs_signature.hpp:
  [0..4)   magic "LFSG"
  [4..8)   firmware version — packed (major<<16)|(minor<<8)|patch, LE
  [8..72)  ed25519 signature over the manifest digest

A dev build is unsigned; no-op and remove any stale fw.lsig so the spiffs.bin
ships without one. Beta and stable sign.

Standalone (testing):  python3 scripts/sign_fs.py path/to/data_dir
"""

from __future__ import annotations

import hashlib
import struct
import sys
from pathlib import Path

# Reuse the firmware signer's version/key/ed25519 helpers, the single source of
# truth so the FS image and firmware binary stamp the identical version in one
# build.
import sign_firmware  # same scripts/ dir

FS_SIG_MAGIC = b"LFSG"
FS_SIG_NAME = "fw.lsig"
FS_SIG_LEN = 72
FS_SIG_SIGNATURE_LEN = 64
MKSPIFFS_NAME_MAX = 31  # SPIFFS_OBJ_NAME_LEN (32) minus the NUL


def _canonical_files(data_dir: Path) -> list[Path]:
    """Flat data files (excluding fw.lsig), validated for the manifest contract."""
    out: list[Path] = []
    for p in sorted(data_dir.iterdir(), key=lambda q: q.name):
        if p.name == FS_SIG_NAME:
            continue
        if not p.is_file():
            print(
                f"[sign-fs] FATAL: {p} is not a flat file — SPIFFS subdirs break "
                f"the host/device name agreement.",
                file=sys.stderr,
            )
            sys.exit(1)
        try:
            p.name.encode("ascii")
        except UnicodeEncodeError:
            print(f"[sign-fs] FATAL: non-ASCII filename {p.name!r}.", file=sys.stderr)
            sys.exit(1)
        if len(p.name) > MKSPIFFS_NAME_MAX:
            print(
                f"[sign-fs] FATAL: filename {p.name!r} exceeds {MKSPIFFS_NAME_MAX} "
                f"chars; mkspiffs would truncate it and the device digest would "
                f"never match.",
                file=sys.stderr,
            )
            sys.exit(1)
        out.append(p)
    # Bytewise sort on the canonical (ASCII) name; matches std::string < on the
    # device. Re-sort explicitly in case iterdir's order differs.
    out.sort(key=lambda q: q.name.encode("ascii"))
    return out


def _manifest_digest(data_dir: Path) -> bytes:
    h = hashlib.sha256()
    for p in _canonical_files(data_dir):
        name = p.name.encode("ascii")
        content = p.read_bytes()
        h.update(struct.pack("<I", len(name)))
        h.update(name)
        h.update(struct.pack("<I", len(content)))
        h.update(content)
    return h.digest()


def _build_fw_lsig(digest: bytes, fw_version: int, private_seed: bytes) -> bytes:
    signature = sign_firmware._sign_bytes(digest, private_seed)
    assert len(signature) == FS_SIG_SIGNATURE_LEN
    blob = FS_SIG_MAGIC + struct.pack("<I", fw_version & 0xFFFFFFFF) + signature
    assert len(blob) == FS_SIG_LEN
    return blob


def sign_fs(data_dir: Path) -> None:
    sig_path = data_dir / FS_SIG_NAME
    if sign_firmware._channel_is_dev():
        # Dev build ships no signature. Drop any stale one so it isn't packed.
        if sig_path.exists():
            sig_path.unlink()
        print("[sign-fs] skipped: dev channel (no fw.lsig produced)")
        return

    private_seed = sign_firmware._read_private_key(sign_firmware.PRIVATE_KEY_PATH)
    fw_version, fw_human = sign_firmware._parse_firmware_version()

    # Exclude any prior fw.lsig from the digest by removing it first, so a
    # stale file from a previous build never feeds into the new signature.
    if sig_path.exists():
        sig_path.unlink()

    digest = _manifest_digest(data_dir)
    sig_path.write_bytes(_build_fw_lsig(digest, fw_version, private_seed))
    print(
        f"[sign-fs] {sig_path.name} written: version={fw_human} "
        f"(0x{fw_version:08x}) digest={digest.hex()[:16]}..."
    )


def _pre_buildfs_action(source, target, env):  # noqa: ARG001 — PIO signature
    data_dir = Path(env.subst("$PROJECT_DATA_DIR"))
    sign_fs(data_dir)


try:
    Import("env")  # type: ignore[name-defined]  # injected by SCons/PIO
    sign_firmware._resolve_paths_pio(env)  # type: ignore[name-defined]
    env.AddPreAction(  # type: ignore[name-defined]
        "$BUILD_DIR/spiffs.bin",
        _pre_buildfs_action,
    )
except NameError:
    if __name__ == "__main__":
        sign_firmware._resolve_paths_cli()
        if len(sys.argv) != 2:
            print(
                "Usage: sign_fs.py <path/to/data_dir>\n"
                "       (writes fw.lsig into the data dir)",
                file=sys.stderr,
            )
            sys.exit(2)
        sign_fs(Path(sys.argv[1]).resolve())
