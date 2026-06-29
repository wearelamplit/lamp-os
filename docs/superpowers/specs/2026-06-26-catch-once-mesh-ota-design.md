# Catch-Once Mesh OTA Receiver on `main` — Design

**Status:** Design (brainstormed + adversarially audited; ready for implementation plan)
**Branch:** `main-mesh-ota` (off `main`)
**Date:** 2026-06-26

## Goal

Add the minimum capability to the OLD `main` firmware so a **standard** main lamp,
when it encounters a lamp running the NEW (`dev`) firmware in a `standard`/`stable`
build on the ESP-NOW mesh, **receives that firmware, verifies it (ed25519), and boots
it** — permanently transitioning itself from main → dev.

This lets us build + ship new standard lamps on `main` **now** and have them
self-upgrade to dev later (e.g. when a stable dev lamp shows up at an event), without
finalizing the dev work first.

**Lifecycle:** one-time, one-directional. The module runs only until the lamp catches
one update and flips to dev; after that it's a dev lamp and never runs this code again.
Receive-only — no distributor.

## Scope / non-goals (YAGNI)

- **Standard build only.** Non-standard lamps use separate custom lamp files and don't
  get this module → **no runtime variant gate needed**.
- No distributor, no rendering/app changes, no ongoing main-side mesh participation.
- No protocol RX-*range* — pin to a single version (0x05).
- **Config is preserved.** dev reads main's NVS correctly — empirically validated by
  USB-flashing main→dev with settings surviving. So **no NVS wipe**, no migration code.

## Why this shape (what the audit changed)

Four expert audits (protocol, radio/coex, OTA/partition/boot, security) reshaped the
naive "minimal bespoke receiver" idea. Key corrections, all folded in below:

- main has **no ESP-NOW and sits on WiFi channel 6**; dev's ESP-NOW is on **channel 11**.
  Without a channel pin, main hears nothing while looking healthy. (#1 silent-failure)
- dev drops HELLOs outside protocol `[0x04,0x05]` → main must emit **0x05**.
- Hardcoding `"standard-stable"` mismatches dev's actual compiled channel (`"stable"`),
  and the gate is channel-only → **omit the channel TLV** and let dev offer anyway.
- `firmwareVersion == 0` is dev's "unknown" sentinel and is skipped → emit **≥ 1**.
- The handshake must **echo dev's values** (seq/version), be **addressed to dev's MAC**,
  use a **fresh seq per REQ**, and **dedup chunks by index** — or frames are silently dropped.
- **JIT-erase is wrong here:** the ESP-NOW recv runs on the WiFi task regardless, so a
  per-sector erase during the stream reintroduces dev's ~60% loss, *and* main has no
  watchdog headroom → a sector erase trips `TG1WDT` and resets mid-OTA. Must use dev's
  **upfront erase + IWDT widening**.
- **"Drop the AP" is not enough:** main's 30s blocking WiFi scan, 2s stage-mode WiFi
  teardown, and continuous BLE active-scan each yank the radio off-channel mid-transfer.
  Need a real **OTA-quiesce gate** (which main lacks today).
- **Good news (verified):** main and dev partition tables are **byte-identical**; the dev
  image **fits with ~21% headroom**; and `set_boot_partition` is gated behind a full
  ed25519 verify, so corrupt/partial/forged-code images **fail closed** — no brick.

## Architecture

A self-contained, disposable module. OTA *mechanics* are **ported verbatim from dev**
(proven + must-match-for-interop). The *transport + radio gating* are **bespoke on main**
(net-new — main has none).

### Copied verbatim from dev (do NOT reimplement — interop/correctness critical)

Sources on `dev`: `software/lamp-os/src/components/...`

- **Wire format** (`HELLO` + `MSG_FW_OFFER/CHUNK/DONE/ACCEPT/REQ/RESULT` structs +
  serialize/parse), from `network/lamp_protocol.hpp`. Pin protocol byte = **0x05**.
- **Signature verify** (ed25519 over streaming SHA-256 of the signed region + LSIG footer
  parse), from `firmware/firmware_signature.*`. Bake in **dev's public key**.
- **OTA flash mechanics**, from `firmware/firmware_receiver.cpp`:
  - **Upfront full-region erase** of the inactive slot **before ACCEPT/streaming** (no RX
    in flight at that point), then pure `esp_partition_write` per chunk — never erase
    during the stream.
  - **IWDT widening**: `custom_sdkconfig` `CONFIG_ESP_INT_WDT_TIMEOUT_MS=1000` +
    the runtime `widenIwdt()` HAL hack around the multi-second erase.
  - **Verify → `esp_ota_set_boot_partition` LAST** ordering (otadata flipped only after a
    passing verify) for power-cut safety.
  - The torn-write barrier (drain in-flight writes before the verify read).
- **Handshake semantics** (the protocol details a naive loop gets wrong):
  - `ACCEPT` echoes `OFFER.seq` → `offerSeq` and `OFFER.version` → `version`
    (**dev's** version, not main's low one); `RESULT` echoes `OFFER.version`.
  - All replies (`ACCEPT/REQ/RESULT`) addressed to `OFFER.sourceMac` (**dev's MAC**).
  - **Fresh `seq` per distinct REQ** (dev's dedup ring drops repeats).
  - **Dedup received CHUNKs by `chunkIdx`, not `seq`** (re-served chunks get new seqs);
    OFFER/DONE retries handled idempotently.

### Bespoke on main (net-new)

- **ESP-NOW link**: `esp_now_init` + a **light** recv callback (queue to the loop task —
  never flash in the callback) + broadcast/unicast send + `esp_now_add_peer` (broadcast at
  init, the sender MAC on first frame, `channel=0` = current channel).
- **HELLO emitter**: periodically broadcast a well-formed **v0x05** HELLO — magic `L,M`,
  a **unique name**, main's real **STA MAC**, `firmwareVersion = 0.0.1`
  (≥1 and < dev's `0x0001007B`, little-endian), **no channel TLV** (`tlv_count=0`).
- **Radio / OTA-quiesce gate** (the load-bearing bespoke correctness piece; main has none):
  on entering receive —
  - Pin WiFi to **channel 11** (`LAMP_ESPNOW_CHANNEL`); re-assert after any mode change.
  - **Disconnect STA** (`WiFi.disconnect(true)`) **and** drop softAP
    (`softAPdisconnect(true)`); go STA-only.
  - **Freeze** the 30s blocking WiFi scan (`updateNetworkScan`) and the 2s stage-mode WiFi
    teardown (`handleStageMode`) behind a new `otaInProgress` flag.
  - **Stop NimBLE scan** with a no-restart flag (the `onScanEnd` self-restart must be
    gated) + pause advertising.
  - Keep `WiFi.setSleep(false)` (main already sets this — don't regress).
- **Rollback circuit-breaker**: a reboot-surviving fail counter (NVS key) — "flashed image
  digest X, failed N times"; stop re-flashing the same image after N attempts. Cheap
  insurance against a verified-but-boots-bad image (low probability given USB validation).

## Flow

1. Standard main lamp boots (main + module), pins WiFi to **ch 11**, inits ESP-NOW, emits a
   **v0x05** HELLO (name, STA MAC, **v0.0.1**, no channel TLV) every ~5s.
2. A dev `standard`/`stable` sender's `considerPeerForOta` sees it (protocol 0x05 in range,
   version 1 < dev's, channel unknown → offer-anyway) → emits `OFFER`.
3. main's OFFER handler (no channel check) → set `otaInProgress` → **quiesce radio**
   (ch11 pin, STA/AP down, scan/stage-mode frozen, BLE scan stopped) → **upfront-erase the
   inactive slot** → `ACCEPT` (echo `OFFER.seq` + `OFFER.version`, addressed to dev's MAC).
4. dev streams `CHUNK`s → main writes each (pure `esp_partition_write`, dedup by `chunkIdx`)
   → `REQ` gaps (fresh `seq` each).
5. `DONE` → **ed25519 verify** the LSIG footer against dev's pubkey → on pass,
   `esp_ota_set_boot_partition` (last) → `RESULT(success, echo OFFER.version)` → reboot.
6. Lamp boots the dev image (`PENDING_VERIFY`), **reads main's NVS (config migrates)**, comes
   up healthy, self-marks-valid → **now a dev lamp.** Module gone.
   - On verify FAIL or a boot-bad rollback: stays / returns to main; the breaker counts the
     attempt and retries next encounter unless N exceeded.

## Compat invariants (must match dev exactly)

| invariant | value |
|---|---|
| Protocol byte (emit + parse-accept) | `0x05` |
| HELLO channel TLV | **omitted** (`tlv_count=0`) |
| HELLO `firmwareVersion` | `0.0.1` — ≥1 and < `0x0001007B`, little-endian at HELLO[20..23] |
| HELLO name / MAC | non-empty unique name; real STA MAC at HELLO[6..11] |
| `ACCEPT` | `offerSeq=OFFER.seq`, `version=OFFER.version`, `targetMac=OFFER.sourceMac` |
| `REQ` | fresh `seq` each; `targetMac=dev`; `chunkCount∈[1,32]` |
| `RESULT` | `version=OFFER.version`; `targetMac=dev` |
| chunk dedup | by `chunkIdx` (never by `seq`) |
| RF channel | `LAMP_ESPNOW_CHANNEL` = **11**, pinned |
| signing key | dev's ed25519 public key, baked in |

## Testing

- **Native unit tests**: the copied wire parse + signature verify against dev's vectors
  (mirror dev's `test_protocol_*`, `test_firmware_signature`, `test_firmware_receiver`).
- **Hardware**: one dev `standard`/`stable` sender + one standard main+module lamp →
  discovery → OFFER → receive → verify → boot-to-dev → **config preserved**. Keep the bench
  to just dev + one main lamp (dev's `peerHigherSeen` suppression: any higher-version lamp
  in earshot stops dev offering).

## Residual risks (accepted)

- **Footer channel/version are outside the signed region** (a pre-existing dev weakness): a
  malicious sender holding *any* dev-signed image could forge the footer channel/version
  (cross-variant / downgrade). Low risk for a trusted-event upgrade; not main-specific;
  out of scope here.
- **Protocol-version skew window**: main pins `0x05`. If dev's protocol moves past `0x05`
  before these lamps upgrade, they won't interop until they meet a `0x05`-compatible sender.
  Acceptable for the ship-now-upgrade-later window.
