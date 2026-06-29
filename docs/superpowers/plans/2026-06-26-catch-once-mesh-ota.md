# Catch-Once Mesh OTA Receiver Implementation Plan (v3 — post re-audit)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a disposable, receive-only mesh-OTA module to the OLD `main` firmware so a standard main lamp self-upgrades to the dev firmware when it meets a dev `standard-stable` sender on the ESP-NOW mesh.

**Architecture:** A new self-contained `catch_ota` component on `main`. OTA *mechanics* (wire format, ed25519 verify, upfront-erase + write, the handshake) are **ported verbatim from dev** and must match byte-for-byte for interop. The transport + radio gating are **net-new bespoke**. On catching one update the lamp verifies, flips the boot partition, and reboots into dev permanently.

**Tech Stack:** ESP32 / Arduino / PlatformIO (pioarduino); ESP-NOW; mbedTLS SHA-256 + libsodium ed25519 (ESP-IDF bundle, on-device) with host stubs for native tests; `esp_ota_ops`/`esp_partition`; NimBLE; Unity (native).

**Reference (dev branch, READ-ONLY):** `/Users/jerrett/projects/lamp-os/software/lamp-os/src/components/network/lamp_protocol.hpp`, `.../firmware/firmware_signature.{hpp,cpp}`, `.../firmware/firmware_receiver.{hpp,cpp}`, `.../network/espnow_link.cpp`, `.../network/wifi.cpp`; host test seams `.../test/test_firmware_signature/{mbedtls/sha256.h, firmware_signature.cpp:56-70}`, `.../test/test_protocol_v2/`, dev signed vector `.../.pio/build/upesy_wroom_standard/firmware-signed.bin`. **Spec:** `docs/superpowers/specs/2026-06-26-catch-once-mesh-ota-design.md`.

## Global Constraints

- **Protocol byte = `0x05`** (emit + parse-accept).
- **HELLO**: magic `'L','M'`; STA MAC at `[6..11]`; non-empty unique name; **`firmwareVersion = 1`** (any value ≥1 — `0` is dev's "unknown" sentinel and is skipped; the accept gate is `offer.version > ours`, so `1` is safely below any dev build and is NOT coupled to dev's exact version number); packed `(major<<16)|(minor<<8)|patch` LE at `[20..23]`; **no channel TLV** (`tlv_count=0`); pass **zeroed** shade+base RGBW arrays to `buildHello` (it returns 0 on null).
- **Accept gate**: act on an OFFER only when `offer.version > ours` (i.e. `> 1`) — dev's image version is far higher; an offer at-or-below ours is declined.
- **`ACCEPT`**: `offerSeq=OFFER.seq`, `version=OFFER.version` (dev's), `targetMac=OFFER.sourceMac`. **`REQ`**: fresh `seq` each; `chunkCount∈[1,32]`. **`RESULT`**: `version=OFFER.version`, `targetMac=dev`. **Dedup chunks by `chunkIdx`, never `seq`.**
- **RF channel = 11** (`LAMP_ESPNOW_CHANNEL`), **pinned from boot** via `esp_wifi_set_channel` (NOT by changing `WIFI_PREFERRED_CHANNEL`, which is shared with STA stage-mode). The softAP therefore runs on ch11 for all boots — **user-accepted** (it's only the web-config channel; main has no mesh for it to break).
- **Expected image channel = `"standard-stable"`** — a hardcoded constant `kExpectChannel`; the ported `verifyAndApply` channel-checks the footer against it. dev's ed25519 **public key** baked in; verify before `esp_ota_set_boot_partition` (called **last**).
- **Upfront full-region erase** before ACCEPT; pure `esp_partition_write` per chunk; **INT_WDT widened to 1000ms via `custom_sdkconfig`** + the runtime `widenIwdt()` (per-64KB-block) and the `esp_task_wdt_reconfigure(30000)`/`restoreDefaultWdt()` pair ported from dev. **Standard build only. NVS preserved** (no wipe).

---

## File Structure

New, all under `src/components/catch_ota/` (deletable in one move post-migration): `ota_protocol.{hpp,cpp}`, `ota_signature.{hpp,cpp}` + `dev_pubkey.h`, `ota_receiver.{hpp,cpp}`, `espnow_link.{hpp,cpp}`, `hello_emitter.{hpp,cpp}`, `radio_quiesce.{hpp,cpp}`, `rollback_breaker.{hpp,cpp}`, `catch_ota.{hpp,cpp}`. Native tests under `test/test_catch_ota_{protocol,signature,breaker,hello}/`. **Modified:** `package.json`, `platformio.ini`, `src/lamps/standard_lamp.cpp`, `src/components/network/wifi.{hpp,cpp}`, `src/components/network/bluetooth.{hpp,cpp}` (both — the new BLE-stop decls go in the header).

**Native-test compile note:** main's `[env:native]` does NOT auto-compile `src/`. Each native test `.cpp` must `#include` its component's `.cpp` (e.g. `#include "../../src/components/catch_ota/ota_protocol.cpp"`) **after** defining any mocks — exactly how dev's `test_firmware_signature/firmware_signature.cpp:70` pulls in the source. Tasks 1–4 each follow this pattern.

---

### Task 0: Scaffolding — build/test/sdkconfig on `main` (the audit's #1 blocker)

main is the *old* tree: no `lamp:*` npm tasks, no native Unity harness, no `custom_sdkconfig`. Stand these up first or nothing downstream runs.

**Files:** Modify `package.json`, `platformio.ini`; Create `test/test_native_smoke/smoke.cpp`, `src/components/catch_ota/catch_ota.{hpp,cpp}` (stub), `src/components/catch_ota/dev_pubkey.h`.

- [ ] **Step 1 — npm tasks.** Add to root `package.json` scripts (must `cd software/lamp-os` first — the root has no `platformio.ini`, and bare `flash` targets lamp-ui): `"lamp:build": "cd software/lamp-os && pio run -e upesy_wroom"`, `"lamp:test": "cd software/lamp-os && pio test -e native"`, `"lamp:flash": "cd software/lamp-os && pio run -e upesy_wroom -t upload"`.
- [ ] **Step 2 — native Unity env.** In `platformio.ini` `[env:native]`: add `test_framework = unity` and `build_flags = -std=gnu++2a -I src -I test/stubs`. Create `test/stubs/mbedtls/sha256.h` by copying dev `test/test_firmware_signature/mbedtls/sha256.h` (vendored public-domain SHA-256). (Native tests compile the component via the `#include "../../src/.../X.cpp"` pattern noted above — there is no `test_build_src`.)
- [ ] **Step 3 — sdkconfig.** Add to the `upesy_wroom` env: `custom_sdkconfig = CONFIG_ESP_INT_WDT_TIMEOUT_MS=1000` (the pioarduino-correct override; a bare `sdkconfig.defaults` is ignored by the precompiled Arduino libs). Add build flags `-D CATCH_OTA_ENABLED=1 -D LAMP_ESPNOW_CHANNEL=11`.
- [ ] **Step 4 — stub + smoke test.** `catch_ota.hpp`: `namespace catch_ota { void begin(); void tick(uint32_t nowMs); bool isInProgress(); }`. `catch_ota.cpp`: empty bodies (`isInProgress`→`false`), `#include <esp_now.h>`+`<sodium.h>` under `#if defined(ARDUINO)||defined(ESP_PLATFORM)`. `dev_pubkey.h`: the 32-byte dev ed25519 public key. `test/test_native_smoke/smoke.cpp`: one `TEST_ASSERT_TRUE(true)`.
- [ ] **Step 5 — verify.** `npm run lamp:test` → smoke PASSES (native runner works). `npm run lamp:build` → SUCCESS (custom_sdkconfig + esp_now/sodium link on-device; check the build log shows the reconfigured INT_WDT value).
- [ ] **Step 6 — commit** `build(catch_ota): scaffold npm tasks, native unity harness, IWDT custom_sdkconfig, component stub`.

---

### Task 1: OTA wire format (protocol port)

**Files:** Create `src/components/catch_ota/ota_protocol.{hpp,cpp}`; Test `test/test_catch_ota_protocol/protocol.cpp`.
**Produces:** `ParsedHello`, `ParsedFwOffer/Chunk/Done`; `buildHello`, `buildFwAccept`, `buildFwReq`, `buildFwResult`; `parseHello`, `parseFwOffer`, `parseFwChunk`, `parseFwDone` — **byte layouts identical to dev `lamp_protocol.hpp`**, protocol byte hardcoded `0x05`.

- [ ] **Step 1:** Copy ONLY the OTA subset from dev `lamp_protocol.hpp` (magic/version consts, MSG_FW_* ids, the listed structs + build/parse fns, `FW_CHANNEL_LEN`/`FW_SHA256_PREFIX_LEN`). Hardcode emit byte `0x05`. `buildHello` emits `tlv_count=0` (pass `fwChannel=nullptr`, `otaState=idle`, **zeroed** shade/base RGBW). Drop all non-OTA messages.
- [ ] **Step 2:** Failing round-trip + invariant tests (test `.cpp` `#include`s `ota_protocol.cpp`): `test_offer_roundtrip` (build per dev layout → `parseFwOffer` → field-equal), `test_hello_proto_magic_and_no_tlv` (`buf[0..1]=='L','M'`, `buf[2]==0x05`, tlv_count==0, version LE at `[20..23]`==1), `test_short_offer_rejected`, `test_req_chunkcount_range` (count 1 and 32 ok, 0/33 rejected).
- [ ] **Step 3:** `npm run lamp:test` → FAIL. **Step 4:** Implement; cross-check a real dev OFFER frame parses field-for-field. **Step 5:** PASS. **Step 6:** commit `feat(catch_ota): port OTA wire format (v0x05, no channel TLV)`.

---

### Task 2: ed25519 LSIG signature verify (port dev verbatim + host seams)

**Files:** Create `src/components/catch_ota/ota_signature.{hpp,cpp}`; Test `test/test_catch_ota_signature/signature.cpp`.
**Produces (dev's EXACT signature — do not invent):**
```cpp
using FirmwareByteReader = std::function<int(size_t offset, size_t wantBytes, uint8_t* out)>; // returns bytes read, -1 on err
bool verifySignedFirmware(FirmwareByteReader reader, size_t imageLen,
                          const char** outChannel, uint32_t* outVersion);
```

- [ ] **Step 1:** Copy dev `firmware_signature.{hpp,cpp}` verbatim except namespace. **Keep the host seam**: `#include <sodium/...>` stays under `#if defined(ARDUINO)||defined(ESP_PLATFORM)`; on host it calls `extern test_crypto_sign_ed25519_verify_detached` (the mock). Point the pubkey include at the new `dev_pubkey.h`. SHA-256 from `test/stubs/mbedtls/sha256.h` on host, real mbedTLS on device.
- [ ] **Step 2:** Failing tests mirroring dev `test_firmware_signature/` (the test `.cpp` defines `int g_verifyRc` + `test_crypto_sign_ed25519_verify_detached(...)`, then `#include`s `ota_signature.cpp`): bad magic, too-short, null reader, oversize/zero signed region, short-read, footer channel+version extraction, multi-block streaming, and `g_verifyRc`-driven accept/reject. **NOTE:** native tests validate footer-parse + SHA-streaming + control flow ONLY — real ed25519 accept/reject of a genuine image is a **Task 9 (hardware)** assertion (the host crypto is mocked). Do not name a native test as if it proves real crypto.
- [ ] **Step 3:** FAIL. **Step 4:** Port until pass. **Step 5:** PASS + full native suite green. **Step 6:** commit `feat(catch_ota): port ed25519 LSIG verify (dev signature + host seams)`.

---

### Task 3: Rollback circuit-breaker (NVS fail-counter)

**Files:** Create `src/components/catch_ota/rollback_breaker.{hpp,cpp}`; Test `test/test_catch_ota_breaker/breaker.cpp`.
**Produces:** `bool shouldAttempt(const uint8_t sha256Prefix[8])`, `void recordAttempt(const uint8_t[8])`. NVS namespace `"catchota"`, keys `failsha`(8B)+`failn`(u8), `kMaxAttempts=3`. KV backend injectable (real `nvs_*` on device, in-memory map on host).

- [ ] **Step 1:** Failing tests (test `.cpp` `#include`s `rollback_breaker.cpp` with the in-memory KV): first attempt allowed; blocked after 3 same-image fails; new image (different prefix) resets. **Step 2:** FAIL. **Step 3:** Implement with the injectable KV. **Step 4:** PASS. **Step 5:** commit `feat(catch_ota): NVS rollback circuit-breaker`.

---

### Task 4: Bespoke ESP-NOW link

**Files:** Create `src/components/catch_ota/espnow_link.{hpp,cpp}`.
**Produces:** `using RecvFn = void(*)(const uint8_t mac[6], const uint8_t* data, size_t len); bool espnowBegin(RecvFn); bool espnowSend(const uint8_t mac[6], const uint8_t* data, size_t len); void espnowAddPeer(const uint8_t mac[6]);`

- [ ] **Step 1:** Port the shape from dev `espnow_link.cpp`: `esp_now_init` → `register_recv_cb(trampoline)` → `esp_now_add_peer(FF:FF:FF:FF:FF:FF, ifidx=WIFI_IF_STA, channel=0)`. The trampoline (WiFi task) ONLY copies into a **fixed SPSC ring: 16 slots × 250B, head/tail indices, overflow-drop** — never flash. `catch_ota::tick()` drains it on the loop. `espnowAddPeer(mac)` adds the sender (channel 0) before the first unicast or `esp_now_send` returns `ESP_ERR_ESPNOW_NOT_FOUND`. (dev's `EspNowRecvFn` carries an rssi arg we don't need — adapt, don't lift the trampoline verbatim.)
- [ ] **Step 2:** Build gate: `npm run lamp:build` SUCCESS; `static_assert(slot_bytes>=250)`. **Step 3:** commit `feat(catch_ota): bespoke ESP-NOW link (16-slot SPSC ring, queue-not-flash)`.

---

### Task 5: HELLO emitter

**Files:** Create `src/components/catch_ota/hello_emitter.{hpp,cpp}`.
**Produces:** `void helloTick(uint32_t nowMs);` broadcasts a v0x05 HELLO every `kHelloIntervalMs=5000` via `espnowSend(BROADCAST, composeHello(...))`; name `"m-"+last3MacHex`, STA MAC, `fwVersion=1`. Suppressed while `catch_ota::isInProgress()`.

- [ ] **Step 1:** Failing test `test_emitted_hello_accepted_by_parser` (test `.cpp` `#include`s the source): `parseHello(composeHello(buf, MAC, "m-abc"))` → true; assert `buf[2]==0x05`, name, MAC, `firmwareVersion==1`, `tlvCount==0`. (Use `composeHello` directly behind a `SendFn` seam.)
- [ ] **Step 2:** FAIL. **Step 3:** Implement. **Step 4:** PASS. **Step 5:** commit `feat(catch_ota): v0x05 HELLO emitter`.

---

### Task 6: Radio-quiesce gate — **ch11 pinned from boot**

**Files:** Create `src/components/catch_ota/radio_quiesce.{hpp,cpp}`; Modify `src/components/network/wifi.{hpp,cpp}` (add `volatile bool otaInProgress`, early-return `updateNetworkScan()` at `wifi.cpp:235` when set), `src/components/network/bluetooth.{hpp,cpp}` (file-scope `bool g_suppressBleRestart` that the file-static `ScanCallbacks::onScanEnd` checks before re-`start()`; add `bleStopScanNoRestart()`/`bleStopAdvertising()` **declared in `bluetooth.hpp`** so `radio_quiesce.cpp` can link them), `src/lamps/standard_lamp.cpp` (gate `handleStageMode()` at `:298` behind `!catch_ota::isInProgress()`).
**Produces:** `void radioBeginDiscovery();` (from `catch_ota::begin()`), `void radioEnterOtaMode();` (on ACCEPT).

- [ ] **Step 1 — discovery channel.** `radioBeginDiscovery()`: after `WiFi.softAP(...)` is up, call `esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE)`. **Do NOT change the `WIFI_PREFERRED_CHANNEL` macro** — it's shared with `toStageMode()`'s STA association (`wifi.cpp:216`) and would force stage/ArtNet onto ch11. This moves the web-config softAP to ch11 for all boots (user-accepted). Add `#include <esp_wifi.h>` (absent from main today). This is the chicken-and-egg fix: main must be on ch11 to be discovered at all.
- [ ] **Step 2 — transfer quiesce.** `radioEnterOtaMode()`: `otaInProgress=true` → `WiFi.disconnect(true)` (STA) → `WiFi.softAPdisconnect(true)` → `WiFi.mode(WIFI_MODE_STA)` → re-assert `esp_wifi_set_channel(LAMP_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE)` → `bleStopScanNoRestart(); bleStopAdvertising()`. Keep `WiFi.setSleep(false)`.
- [ ] **Step 3:** Build SUCCESS. Document the call order as a checklist (verified on hardware in Task 9). **Step 4:** commit `feat(catch_ota): radio gate — ch11 from boot, STA/AP/BLE quiesce on accept`.

---

### Task 7: Receive state machine (port + trim to single-threaded)

**Files:** Create `src/components/catch_ota/ota_receiver.{hpp,cpp}`.
**Consumes:** `ota_protocol::*`, `ota_signature::verifySignedFirmware`, `rollback_breaker::*`, `espnowSend`, `radioEnterOtaMode`.
**Produces:** `void onOffer(const ParsedFwOffer&, const uint8_t devMac[6]); void onChunkOnLoop(const ParsedFwChunk&); void onDone(const ParsedFwDone&); void tick(uint32_t); bool isInProgress();` — states `Idle/Streaming/Verify/Failed` (**no `Accepted` state** — transition OFFER→`Streaming` directly and set `state_=Streaming` *before* sending ACCEPT, exactly as dev does, so the first chunks pass the `state_==Streaming` guard).

- [ ] **Step 1 — port-and-trim from dev `firmware_receiver.cpp`.**
  - **KEEP:** inactive-partition pick; the `erasedForLen_ == offerTotalLen_` coverage latch (re-OFFER-at-different-length safety), demoted to a plain member; **upfront full-region erase before ACCEPT**, with `widenIwdt()` per 64KB block AND the TWDT pair (`esp_task_wdt_reconfigure(30000)` on the OFFER path, `restoreDefaultWdt()` on every exit); per-chunk `esp_partition_write`; the received-chunk **bitmap** + `firstMissingRunLen` REQ logic; `verifyAndApply` (→ `verifySignedFirmware` → check `outVersion==offerVersion_` and `outChannel`==`kExpectChannel` (`"standard-stable"`) → `esp_ota_set_boot_partition` **LAST** → reboot).
  - **DELETE (single-threaded — no cross-core race):** the `publishedOtaHandle_` gate, `eraseMux_`, **and the `writesInFlight_` drain inside `verifyAndApply`** (the drain IS the cross-core teardown — it goes too). Demote any remaining atomics to plain members. Replace the gate's "drop stray chunk" role with the `state_==Streaming` guard at the top of `onChunkOnLoop`.
  - **DELETE:** the distributor; the BLE-OTA transport; **the `ota_quiet_mode.hpp` / `ble_control.hpp` couplings** (`enterQuiet`/`pauseRadioForOta` calls — replaced by `radio_quiesce`, invoked by the orchestrator *before* `onOffer`); the `channelMatchesOurs` **silent-drop in `onOffer`** (we discover via no-TLV HELLO; the post-transfer `verifyAndApply` check against `kExpectChannel` is the gate).
  - **HANDSHAKE (exact):** `ACCEPT` echoes `OFFER.seq`+`OFFER.version` to `OFFER.sourceMac`; `REQ` fresh seq each; `RESULT` echoes `OFFER.version`; **dedup chunks by `chunkIdx`**.
- [ ] **Step 2:** Build SUCCESS + full native suite green (no native receiver unit test — safety/interop invariants validated on hardware per Task 9). **Step 3:** commit `feat(catch_ota): single-threaded receive state machine (upfront-erase, echo-dev handshake)`.

---

### Task 8: Orchestrator + standard_lamp integration

**Files:** Modify `src/components/catch_ota/catch_ota.cpp`; `src/lamps/standard_lamp.cpp`.

- [ ] **Step 1:** `catch_ota::begin()` → `radioBeginDiscovery()` + `espnowBegin(onEspnowRecv)`. `onEspnowRecv` (WiFi task) only enqueues. `tick()` drains the ring: **on an OFFER with `offer.version > ours` (`> 1`)**, call `rollback_breaker::shouldAttempt(offer.sha256Prefix)`; if true → `radioEnterOtaMode()` + `recordAttempt` + `receiver.onOffer(...)`; CHUNK→`onChunkOnLoop`; DONE→`onDone`. Run `helloTick` (suppressed while `isInProgress`) + `receiver.tick`.
- [ ] **Step 2:** `standard_lamp.cpp`: after `bt.begin()`+`wifi.begin()` (`:289`) add `#if CATCH_OTA_ENABLED` `catch_ota::begin();`; in `loop()` (`:297`) add `catch_ota::tick(millis());` and gate the existing `handleStageMode()` (`:298`) behind `!catch_ota::isInProgress()`.
- [ ] **Step 3:** Build SUCCESS + native suite green. **Step 4:** commit `feat(catch_ota): orchestrator + standard_lamp integration`.

---

### Task 9: Hardware end-to-end validation (THE load-bearing test — explicit pass/fail)

**Files:** none.

- [ ] **Step 1:** Flash one standard lamp with `main`+module (`npm run lamp:flash`); flash a second lamp with a dev `standard-stable` build (sender). Bench = only these two (dev's `peerHigherSeen` suppression).
- [ ] **Step 2 — discovery + transfer, with thresholds:**
  - Sender log shows it discovers the main lamp (by its STA MAC) and emits an OFFER **targeted to that MAC**.
  - Main log: `radioEnterOtaMode` (ch11, AP+BLE down); **upfront erase completes with NO `TG1WDT` reset** (record the ms — dev measured ~4s for the full ~1.5MB region, far over the 1000ms baseline, which is why `widenIwdt()` yields per block); chunk receive reaches **the full image (~7758 chunks / ~1,551,616 B)**; REQ recovery fires on gaps; `ed25519 rc=0`; `set_boot_partition` only **after** the rc=0 line; `RESULT(success)` observed on the sender.
- [ ] **Step 3 — outcome:** lamp **boots into dev** and **prior config survives** (name/colors).
- [ ] **Step 4 — safety:** (a) power-cut **mid-transfer** → boots valid `main`; (b) power-cut **after `set_boot` before reboot** → boots dev or valid main, never a half image; (c) **reboot-on-failure recovery:** corrupt the sender image → on verify-fail the lamp **reboots and comes back on `main` with its web AP restored**, then re-attempts on the next encounter; after **3 committed attempts the breaker stops** → lamp boots `main`, AP up, no further attempts (no infinite reboot loop). Confirm the AP is genuinely back after a failed attempt (this is the radio-recovery the reboot provides).
- [ ] **Step 5:** Record timings/erase-ms/dup% in a notes file; fix-commit only if issues surface.

---

## Self-Review

**Spec coverage:** scaffolding+sdkconfig (T0), wire format v0x05/no-TLV (T1), ed25519 verify + dev key (T2), breaker (T3), ESP-NOW link (T4), HELLO (T5), **ch11-from-boot** + radio quiesce (T6), single-threaded receiver + echo-dev handshake + idx-dedup + upfront-erase + WDT + channel-via-kExpectChannel (T7), accept-gate `offer.version > ours` + integration (T8), rigorous hardware validation (T9). ✓

**Re-audit fixes folded (v3):** npm tasks `cd software/lamp-os`; **accept gate corrected to `offer.version > ours`** (was inverted); dropped the `Accepted` state (OFFER→Streaming, set Streaming before ACCEPT); version decoupled from dev's exact number (emit `1`); named the `erasedForLen_` latch in KEEP; named the `ota_quiet_mode`/`ble_control` removal; ch11 via `esp_wifi_set_channel` only (not the shared macro) + user-accepted softAP move; `bluetooth.hpp` in the modified list with the BLE-stop decls; native-test `#include "../../src/.../X.cpp"` compile pattern stated. ✓

**Placeholder scan / type consistency:** dev names used verbatim (`verifySignedFirmware`, `buildFwAccept`, `parseFwOffer`, `widenIwdt`, `kExpectChannel`); `onOffer`/`onChunkOnLoop`/`onDone` consistent T7↔T8; no TBD. ✓
