# Lamp-OS firmware OTA tooling

Host scripts for the gossip-OTA mesh. The first two integrate with
PlatformIO to produce signed firmware; the last two are bench-debug
tools for watching OTA cascade behavior across multiple physical
lamps over USB.

| Script | Hook | Role |
|---|---|---|
| `gen_firmware_keys.py` | manual, one-shot | Generate the ed25519 keypair that signs all OTA firmware. |
| `sign_firmware.py` | PIO `post:` on lamp + wisp builds | Append the LSIG footer + ed25519 signature to `firmware.bin`. |
| `bench_tap.py` | manual, on the bench | Tail multiple lamp serial ports with labeled prefixes. |
| `ota_monitor.py` | manual, on the bench | Filter + summarize OTA events out of a tap log. |

The gossip-OTA model: the Flutter app downloads a signed lamp binary
from GitHub Releases, verifies the LSIG footer locally, then pushes it
to a paired lamp over BLE. The lamp accepts the OTA, reboots into the
new version, and then propagates that exact same image to peers it
meets over ESP-NOW mesh — every running lamp is its own
"distributor", sourcing the bytes directly out of its running OTA
partition. No wisp involvement, no embedded blob, no separate
distribution channel.

## One-time setup (per workstation)

```sh
python3 scripts/gen_firmware_keys.py
```

This generates a fresh ed25519 keypair:

- Private key → `~/.lamp-os-firmware-key.bin` (32 bytes, mode 0600).
  **NEVER commit.** Lives outside the repo on purpose.
- Public key → `software/lamp-os/src/components/firmware/firmware_pubkey.h` (32 bytes).
  IS committed. Every lamp firmware build bakes this in; every lamp in
  the fleet verifies incoming OTA against this exact public key.

The script refuses to overwrite an existing private key unless `--force`
is passed.

### Losing or rotating the private key

If you lose `~/.lamp-os-firmware-key.bin`, the existing fleet is
PERMANENTLY locked out of OTA — there's no recovery path over the air
because every lamp will reject any signature not matching its baked-in
`kFirmwarePubkey`. The ONLY way back is:

1. Generate a new keypair (`gen_firmware_keys.py --force`).
2. USB re-flash EVERY lamp with a firmware build containing the new
   `firmware_pubkey.h`.
3. Future OTAs use the new key.

Same path applies to deliberate rotation (e.g. dev key → production key).
Plan accordingly.

## Building signed firmware

A signed binary needs an explicit channel (`beta` or `stable`) plus the key; a
bare build is `dev` (unsigned, `LAMP_DEBUG` on, OTA island).

```sh
cd software/lamp-os && LAMP_FIRMWARE_CHANNEL=beta pio run -e upesy_wroom_standard
  # post-build sign_firmware.py appends the LSIG footer to firmware.bin and
  # writes firmware-signed.bin alongside it. This is what the app pushes over
  # BLE to seed gossip-OTA across the fleet.

cd software/wisp && LAMP_FIRMWARE_CHANNEL=beta pio run -e seeed_xiao_esp32_c6
  # sign_firmware.py runs the same way. The signed wisp binary has no consumer
  # today (wisps don't gossip-OTA), but the hook stays for a future wisp
  # self-OTA path.
```

## Channels

`LAMP_FIRMWARE_CHANNEL` selects one of three: `dev` (default), `beta`,
`stable`. Both the compiled firmware and the LSIG footer carry the same
`{type}-{channel}` slot. `dev` is unsigned, `LAMP_DEBUG` on, and an OTA island
(never signs, never sources OTA); `beta` and `stable` sign and require the key.

```sh
LAMP_FIRMWARE_CHANNEL=beta pio run -e upesy_wroom_standard
```

A `beta` lamp's OFFER carries `channel="…-beta"`; a `stable` peer silently
drops it (a beta lamp accepts a newer stable — promotion). A dev lamp matches
neither. Cross-channel migration only via USB re-flash.

## Files in detail

### `gen_firmware_keys.py`

- Prefers PyNaCl (libsodium bindings); falls back to `cryptography`.
- Round-trip sign+verify self-test before writing files; aborts on
  failure rather than emitting unusable keys.
- Header rewrite is round-trip safe: only the 32 byte values inside the
  `kFirmwarePubkey[32]` literal change; `#pragma once`, namespaces, and
  closing braces are preserved.

### `sign_firmware.py`

- PIO post-build hook for `[env:upesy_wroom]` in
  `software/lamp-os/platformio.ini` and `[env:seeed_xiao_esp32_c6]` in
  `software/wisp/platformio.ini`.
- Reads `firmware.bin` from PIO's build dir, appends 96-byte LSIG footer
  (magic + channel + version + signedRegionLen + reserved + ed25519
  signature), writes `firmware-signed.bin` alongside.
- Version source: the root `VERSION` file. `inject_version.py` reads it and
  injects the `LAMP_FW_*` build defines for lamp + wisp; `sign_firmware.py`
  reads the same `VERSION` file to stamp the footer.
- Idempotent: if `firmware-signed.bin` is newer than `firmware.bin` AND
  newer than the private key, skips re-signing.

### `bench_tap.py`

Multi-port serial tail with labeled prefixes for diagnosing mesh
behavior across two or three USB-connected lamps + a wisp on the
bench. Holds DTR high + RTS low so opening the port doesn't auto-reset
the lamp.

```sh
# Two lamps + a wisp, prefix each line with the role label
scripts/bench_tap.py \
  /dev/cu.usbserial-0001:flora \
  /dev/cu.usbserial-6:gramp \
  /dev/cu.usbmodem11101:wisp

# Same, but write to a file you can grep / tail later
scripts/bench_tap.py -o /tmp/bench.log \
  /dev/cu.usbserial-0001:flora \
  /dev/cu.usbserial-6:gramp
```

If `:LABEL` is omitted, the port basename is used.

### `ota_monitor.py`

Reads a bench-tap log (file or stdin) and surfaces only the lines that
matter for a gossip-OTA flow: OFFER / ACCEPT / REQ / DONE / RESULT /
abort markers, plus pipelined-erase + version transitions.

```sh
# Live-tail the bench log, OTA events only
scripts/ota_monitor.py -f /tmp/bench.log

# Add a one-line emit on every HELLO version transition (the visible
# signal of a successful OTA hop in the cascade)
scripts/ota_monitor.py -f /tmp/bench.log --summary

# Pipe straight from bench_tap.py
scripts/bench_tap.py /dev/cu.usbserial-0001:flora \
                     /dev/cu.usbserial-6:gramp \
  | scripts/ota_monitor.py --summary
```
