#!/usr/bin/env python3
"""PIO post-build hook: append the LSIG footer to firmware.bin.

Wired from software/lamp-os/platformio.ini as
  extra_scripts = post:../../scripts/sign_firmware.py
inside [env:upesy_wroom]. PIO passes the build env via Import("env").

Output: $BUILD_DIR/firmware-signed.bin = firmware.bin || LSIG_footer(96 B).

LSIG footer byte layout (canonical — see
software/lamp-os/src/components/firmware/firmware_signature.hpp):

  offset  size  field
  0       4     magic "LSIG"
  4       8     channel — zero-padded ASCII
  12      4     firmware version — packed (major<<16)|(minor<<8)|patch, LE
  16      4     signedRegionLen — imageLen - 96, LE
  20      12    reserved (zeros)
  32      64    ed25519 signature over SHA256(image[0..signedRegionLen))

NOTE (2026-06-05): the ed25519 signature is over the 32-byte SHA-256
DIGEST of the signed region, not the raw signed region. This is what
lets the lamp verify the image in ~4 KB of stack instead of allocating
the full 1.4 MB firmware buffer on its ~280 KB heap; verifySignedFirmware
streams SHA-256 in 4 KB blocks via the FirmwareByteReader and then
ed25519-verifies the digest. The at-rest LSIG layout is unchanged —
only the contents of the 64-byte signature field changed meaning.

Idempotency: skip re-signing if firmware-signed.bin is newer than both
firmware.bin AND the private key. PIO calls post-build on every link;
without this guard the script would re-sign on every incremental compile
that didn't change the .bin (e.g. partial-link with no source delta).

Also runs standalone (no PIO env) for ad-hoc signing during testing:
  python3 scripts/sign_firmware.py path/to/firmware.bin
The standalone path writes firmware-signed.bin alongside the input.
"""

from __future__ import annotations

import hashlib
import os
import re
import struct
import sys
from pathlib import Path

# `__file__` isn't defined when SCons execs a script (the Import("env")
# loader uses compile()+exec()), so we derive the repo paths lazily:
# - PIO path: env.subst("$PROJECT_DIR") gives software/lamp-os; ../.. is repo.
# - CLI path: __file__ is available; ../scripts/.. is repo.
# Both branches populate the same module-level globals.

PRIVATE_KEY_PATH = Path.home() / ".lamp-os-firmware-key.bin"
REPO_ROOT: Path  # populated by _resolve_paths_*
LAMP_DIR: Path


def _resolve_paths_cli() -> None:
    global REPO_ROOT, LAMP_DIR
    script_dir = Path(__file__).resolve().parent
    REPO_ROOT = script_dir.parent
    LAMP_DIR = REPO_ROOT / "software" / "lamp-os"


def _resolve_paths_pio(env) -> None:
    global REPO_ROOT, LAMP_DIR
    project_dir = Path(env.subst("$PROJECT_DIR")).resolve()
    # PROJECT_DIR is software/lamp-os; repo root is its parent's parent.
    REPO_ROOT = project_dir.parent.parent
    LAMP_DIR = project_dir

# Canonical LSIG layout — mirrors firmware_signature.hpp's offsets exactly.
# v0x04: channel widens 8 → 16 (carries `{type}-{channel}`); reserved shrinks
# 12 → 4. Signature offset 32 is pinned.
LSIG_FOOTER_LEN = 96
LSIG_MAGIC = b"LSIG"
LSIG_CHANNEL_LEN = 16
LSIG_SIGNATURE_LEN = 64
LSIG_RESERVED_LEN = 4
LSIG_CHANNEL_OFFSET = 4
LSIG_VERSION_OFFSET = 20
LSIG_SIGNED_LEN_OFFSET = 24
LSIG_SIGNATURE_OFFSET = 32

DEFAULT_CHANNEL = "stable"


def _read_private_key(path: Path) -> bytes:
    if not path.exists():
        print(
            f"[sign] FATAL: signing key not found at {path}.\n"
            f"       Run `python3 scripts/gen_firmware_keys.py` first.",
            file=sys.stderr,
        )
        sys.exit(1)
    key = path.read_bytes()
    if len(key) != 32:
        print(
            f"[sign] FATAL: private key at {path} is {len(key)} bytes, "
            f"expected 32 (raw ed25519 seed).",
            file=sys.stderr,
        )
        sys.exit(1)
    return key


# Regex for the canonical version.hpp form:
#   constexpr uint32_t FIRMWARE_VERSION = 0xMMmmpp;  // M.m.p
_VERSION_HPP_RE = re.compile(
    r"FIRMWARE_VERSION\s*=\s*0x([0-9a-fA-F]+)"
)
# Regex for the firmware_version.h form the task spec mentions
# (#define FIRMWARE_VERSION_MAJOR/MINOR/PATCH N). Tolerated as a fallback
# for forward compat if/when the project moves to that layout.
_MAJOR_RE = re.compile(r"#define\s+FIRMWARE_VERSION_MAJOR\s+(\d+)")
_MINOR_RE = re.compile(r"#define\s+FIRMWARE_VERSION_MINOR\s+(\d+)")
_PATCH_RE = re.compile(r"#define\s+FIRMWARE_VERSION_PATCH\s+(\d+)")
# Regex for Phase H SemVer form in platformio.ini:
#   -D LAMP_FW_MAJOR=1
#   -D LAMP_FW_MINOR=0
#   -D LAMP_FW_PATCH=82
_LAMP_MAJOR_RE = re.compile(r"-D\s+LAMP_FW_MAJOR\s*=\s*(\d+)")
_LAMP_MINOR_RE = re.compile(r"-D\s+LAMP_FW_MINOR\s*=\s*(\d+)")
_LAMP_PATCH_RE = re.compile(r"-D\s+LAMP_FW_PATCH\s*=\s*(\d+)")


def _parse_firmware_version() -> tuple[int, str]:
    """Return (packed_u32, human_str). Single source of truth: platformio.ini build_flags."""
    # Phase H path: LAMP_FW_* defines in platformio.ini [env:upesy_wroom].
    pio_ini = LAMP_DIR / "platformio.ini"
    if pio_ini.exists():
        text = pio_ini.read_text()
        m_major = _LAMP_MAJOR_RE.search(text)
        m_minor = _LAMP_MINOR_RE.search(text)
        m_patch = _LAMP_PATCH_RE.search(text)
        if m_major and m_minor and m_patch:
            major = int(m_major.group(1))
            minor = int(m_minor.group(1))
            patch = int(m_patch.group(1))
            packed = ((major & 0xFF) << 16) | ((minor & 0xFF) << 8) | (patch & 0xFF)
            return packed, f"{major}.{minor}.{patch}"
    # Fallback path: defines header at include/firmware_version.h (legacy).
    defines_path = LAMP_DIR / "include" / "firmware_version.h"
    if defines_path.exists():
        text = defines_path.read_text()
        m_major = _MAJOR_RE.search(text)
        m_minor = _MINOR_RE.search(text)
        m_patch = _PATCH_RE.search(text)
        if m_major and m_minor and m_patch:
            major = int(m_major.group(1))
            minor = int(m_minor.group(1))
            patch = int(m_patch.group(1))
            packed = ((major & 0xFF) << 16) | ((minor & 0xFF) << 8) | (patch & 0xFF)
            return packed, f"{major}.{minor}.{patch}"
    # Final fallback: src/version.hpp's `FIRMWARE_VERSION = 0xMMmmpp` literal (pre-Phase H).
    version_hpp = LAMP_DIR / "src" / "version.hpp"
    if version_hpp.exists():
        m = _VERSION_HPP_RE.search(version_hpp.read_text())
        if m:
            packed = int(m.group(1), 16) & 0xFFFFFF
            major = (packed >> 16) & 0xFF
            minor = (packed >> 8) & 0xFF
            patch = packed & 0xFF
            return packed, f"{major}.{minor}.{patch}"
    print(
        "[sign] FATAL: could not locate firmware version. Looked at:\n"
        f"       {pio_ini} (LAMP_FW_MAJOR/MINOR/PATCH build_flags)\n"
        f"       {defines_path} (FIRMWARE_VERSION_MAJOR/MINOR/PATCH defines)\n"
        f"       {version_hpp} (FIRMWARE_VERSION = 0xXXXXXX literal)",
        file=sys.stderr,
    )
    sys.exit(1)


def _resolve_channel(env=None) -> bytes:
    """Combine `{custom_lamp_variant}-{LAMP_FIRMWARE_CHANNEL}` into the 16-byte
    LSIG channel field. The PIO env exposes the variant via the per-env
    `custom_lamp_variant` option set in `[env:upesy_wroom_<variant>]`. When
    running without PIO (CLI re-sign), the caller passes
    `LAMP_FIRMWARE_CHANNEL` as the literal combined string."""
    base = os.environ.get("LAMP_FIRMWARE_CHANNEL") or DEFAULT_CHANNEL
    if not base.strip():
        base = DEFAULT_CHANNEL
    variant = None
    if env is not None:
        try:
            variant = env.GetProjectOption("custom_lamp_variant")
        except Exception:
            variant = None
    if variant:
        combined = f"{variant}-{base}"
    else:
        combined = base
    encoded = combined.encode("ascii", errors="strict")
    if len(encoded) > LSIG_CHANNEL_LEN:
        print(
            f"[sign] FATAL: combined channel {combined!r} exceeds {LSIG_CHANNEL_LEN} "
            f"bytes (got {len(encoded)}).",
            file=sys.stderr,
        )
        sys.exit(1)
    return encoded.ljust(LSIG_CHANNEL_LEN, b"\x00")


def _sign_bytes(message: bytes, private_seed: bytes) -> bytes:
    """Sign with ed25519. Try PyNaCl first (mirrors firmware libsodium)."""
    try:
        from nacl.signing import SigningKey  # type: ignore

        return SigningKey(private_seed).sign(message).signature
    except ImportError:
        pass
    try:
        from cryptography.hazmat.primitives.asymmetric.ed25519 import (
            Ed25519PrivateKey,
        )

        sk = Ed25519PrivateKey.from_private_bytes(private_seed)
        return sk.sign(message)
    except ImportError:
        print(
            "[sign] FATAL: neither PyNaCl nor `cryptography` is installed.",
            file=sys.stderr,
        )
        sys.exit(1)


def _build_footer(
    signed_region: bytes, channel: bytes, fw_version: int, private_seed: bytes
) -> bytes:
    """Construct the 96-byte LSIG footer.

    Per firmware_signature.hpp's `verifySignedFirmware`: the signature
    covers SHA-256(image[0..signedRegionLen)) — the 32-byte digest of
    the signed region, not the raw region. The lamp's verify path
    streams SHA-256 over the signed region in 4 KB blocks (no full-
    image heap alloc) and then ed25519-verifies the 64-byte signature
    against that digest. signedRegionLen here equals
    len(signed_region) = imageLen-96.
    """
    assert len(channel) == LSIG_CHANNEL_LEN
    signed_region_len = len(signed_region)
    digest = hashlib.sha256(signed_region).digest()
    assert len(digest) == 32
    signature = _sign_bytes(digest, private_seed)
    assert len(signature) == LSIG_SIGNATURE_LEN

    footer = bytearray(LSIG_FOOTER_LEN)
    footer[0:4] = LSIG_MAGIC
    footer[LSIG_CHANNEL_OFFSET : LSIG_CHANNEL_OFFSET + LSIG_CHANNEL_LEN] = channel
    struct.pack_into("<I", footer, LSIG_VERSION_OFFSET, fw_version & 0xFFFFFFFF)
    struct.pack_into("<I", footer, LSIG_SIGNED_LEN_OFFSET,
                     signed_region_len & 0xFFFFFFFF)
    # bytes [28..32) already zeros from bytearray() — reserved
    footer[LSIG_SIGNATURE_OFFSET : LSIG_SIGNATURE_OFFSET + LSIG_SIGNATURE_LEN] = signature
    assert len(footer) == LSIG_FOOTER_LEN
    return bytes(footer)


def _is_up_to_date(input_bin: Path, signed_bin: Path, private_key: Path) -> bool:
    """v0x04: unconditionally re-sign on every post-build. Earlier versions
    cached via an mtime + footer-channel check, but the freshness check
    silently shipped stale-format footers when the script itself updated
    (its mtime wasn't part of the gate) and the cache savings are
    negligible (signing a 1.5 MB image is ~50ms on the CI runner).
    """
    return False


def sign(input_bin: Path, output_bin: Path, env=None) -> None:
    if not input_bin.exists():
        print(f"[sign] FATAL: input firmware not found: {input_bin}", file=sys.stderr)
        sys.exit(1)

    private_seed = _read_private_key(PRIVATE_KEY_PATH)

    if _is_up_to_date(input_bin, output_bin, PRIVATE_KEY_PATH):
        print(f"[sign] up-to-date: {output_bin.name} (skipping re-sign)")
        return

    raw = input_bin.read_bytes()
    fw_version, fw_human = _parse_firmware_version()
    channel = _resolve_channel(env)

    footer = _build_footer(raw, channel, fw_version, private_seed)
    signed = raw + footer

    output_bin.write_bytes(signed)

    # Also overwrite the input firmware.bin with the LSIG-tailed bytes
    # so PIO's standard upload (which flashes firmware.bin) plants the
    # signed image on the lamp's running partition. Without this the
    # first-flashed lamp can't act as a gossip-OTA distributor: its
    # FirmwareDistributor scans backward for the LSIG magic to discover
    # the image length, and an unsigned firmware.bin has nothing for
    # it to find. The ESP IDF bootloader ignores the 96 trailing bytes
    # (it only reads segment headers from offset 0 onward), so the
    # tail is functionally invisible to boot.
    input_bin.write_bytes(signed)

    sha = hashlib.sha256(raw).hexdigest()[:16]
    channel_str = channel.rstrip(b"\x00").decode("ascii", errors="replace")
    print(
        f"[sign] {output_bin.name} written: "
        f'channel="{channel_str}" version={fw_human} '
        f"(0x{fw_version:08x}) signedLen={len(raw)} totalLen={len(signed)}"
    )
    print(f"[sign]   firmware.bin also overwritten with LSIG-tailed bytes")
    print(f"[sign]   sha256(signed_region)={sha}...")


# ---------------------------------------------------------------------------
# Entry points
# ---------------------------------------------------------------------------


def _post_build_action(source, target, env):  # noqa: ARG001 — PIO signature
    """SCons action signature. `target` is the linker output (.elf)."""
    # Compile-only escape hatch — set LAMP_FIRMWARE_SKIP_SIGN=1 to no-op the
    # post-build hook. Used by CI on PR builds (where the private key
    # isn't materialized from a secret) so that `pio run` succeeds without
    # producing a signed artifact. Release workflows (tag / beta branch)
    # materialize the secret and don't set this flag.
    if os.environ.get("LAMP_FIRMWARE_SKIP_SIGN") == "1":
        print(
            "[sign] skipped: LAMP_FIRMWARE_SKIP_SIGN=1 "
            "(compile-only / PR build — no signed artifact produced)"
        )
        return
    build_dir = Path(env.subst("$BUILD_DIR"))
    input_bin = build_dir / "firmware.bin"
    output_bin = build_dir / "firmware-signed.bin"
    sign(input_bin, output_bin, env)


# PIO entry point: when PIO Import()s this script, it runs at module load.
try:
    Import("env")  # type: ignore[name-defined]  # injected by SCons/PIO
    _resolve_paths_pio(env)  # type: ignore[name-defined]
    env.AddPostAction(  # type: ignore[name-defined]
        "$BUILD_DIR/${PROGNAME}.bin",
        _post_build_action,
    )
except NameError:
    # Not running under PIO — fall through to CLI mode.
    if __name__ == "__main__":
        _resolve_paths_cli()
        if len(sys.argv) != 2:
            print(
                "Usage: sign_firmware.py <path/to/firmware.bin>\n"
                "       (writes firmware-signed.bin alongside the input)",
                file=sys.stderr,
            )
            sys.exit(2)
        in_bin = Path(sys.argv[1]).resolve()
        out_bin = in_bin.with_name("firmware-signed.bin")
        sign(in_bin, out_bin)
