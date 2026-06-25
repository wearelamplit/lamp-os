# Accepted security threats

Status: deliberate-tradeoff accepted on 2026-06-10. This note records the threats we are NOT fixing, the threats we ARE addressing in parallel, and what would change the call.

## Two cleartext-secret threats we are NOT fixing

### T1. `wispOp setWifi` leaks the Wi-Fi PSK in two places

When the user configures the wisp's home Wi-Fi via the app:

1. **App → BLE → connected lamp:** the wispOp JSON `{"char":"wispOp","op":"setWifi","ssid":"…","pw":"…"}` is written to `CHAR_WISP_OP` as plaintext. A passive BLE sniffer in range of the phone captures the PSK.
2. **Lamp → ESP-NOW broadcast → wisp:** the lamp re-broadcasts the same plaintext payload as `MSG_CONTROL_OP` on channel 11 so the wisp can ingest it. An ESP-NOW sniffer in mesh range captures the PSK there.

The mesh-leg leak is the more concerning one — ESP-NOW range is ~30 m LoS, and an attacker doesn't need to be visually near the user.

### T2. First-claim control password is written plaintext

`ControlNotifier.setLampPassword` and the onboarding claim path both take the `LampCrypto.magicPlaintext` branch when `oldPassword` is empty (factory state, post-reset). The new password lands on `CHAR_SETTINGS_BLOB` unencrypted. A passive BLE sniffer in range of the adoption captures one specific lamp's new admin password.

## Why we are not fixing these

Both leaks have the same root cause: **there is no shared secret between the app and a freshly-claimed lamp**, and on the wisp leg, **the wisp has no shared key with the lamp to decrypt with**. The fix that would actually work is **fleet-wide mesh authentication** — every lamp + wisp shares a PSK distributed at provisioning time, used to encrypt mesh-side `MSG_CONTROL_OP` frames and `CHAR_WISP_OP` writes.

We **explicitly rejected** fleet-wide mesh auth earlier in this codebase iteration (commits `f81e1bc` and `f8afc7e` reverted Phases 1+2 of the mesh-auth design). The reasons:

- Per-device MAC tracking, key distribution, rotation, and revocation are operational machinery that doesn't fit the "lamps just work, no central registry" UX philosophy.
- Cross-owner gossip-OTA depends on lamps trusting each other transitively; introducing per-device auth at the mesh layer would constrain that.
- The threat scope is bounded: both leaks require a passive sniffer with the right radio AND physical proximity AT THE MOMENT OF CONFIGURATION. Wi-Fi setup and first-claim are rare, in-person, operator-controlled events.

## What we ARE doing (independent of mesh auth)

Even with mesh auth rejected, several security improvements are worth doing because they're local-device hardening or crypto-isolation:

- **W4.1 — Move control passwords from plaintext SharedPreferences into `flutter_secure_storage`.** Closes the rooted-device / `adb backup` / iOS file access exfiltration path. Independent of mesh radio.
- **W4.3 — HKDF info per-device.** Today two lamps sharing a password derive the same AES key. Mixing the lamp `id` into the HKDF `info` blocks cross-lamp ciphertext-swap and reduces the blast radius of any one compromised device.
- **W7.6 — Cap GATT reads at 4 KB before `jsonDecode`.** A hostile or malformed lamp can't OOM the app via an oversized notification payload.
- **OTA trust chain stays intact** — Ed25519-signed firmware verification (the actual high-value attack surface) is independent and unmodified.

## What would change the call

We'd re-open the mesh-auth design if any of these became true:
- Deployment in a hostile-RF venue (state-sponsored sniffer presence, not curious-attendee-with-an-RTL-SDR).
- A specific high-value secret (a customer-bound API key, a billing token) ended up flowing through the wispOp / setLampPassword paths.
- A nominally trusted owner-of-lamp-X turned attacker-against-lamp-Y in the gossip-OTA cross-owner-update flow — which we explicitly designed FOR rather than against, so this would represent a fundamental shift.

## Code-side breadcrumbs

Search for `// SECURITY:` comments referencing this note:
- `software/lamp-app-flutter/lib/features/wisp/data/wisp_repository.dart` (`setWifi`)
- `software/lamp-app-flutter/lib/features/control/application/control_notifier.dart` (`setLampPassword` empty-old-password branch)
- `software/lamp-app-flutter/lib/features/onboarding/application/add_lamp_notifier.dart` (claim path)
- `software/lamp-app-flutter/lib/core/ble/lamp_crypto.dart` (magic-plaintext byte dartdoc)
- `software/lamp-os/src/components/network/lamp_protocol.hpp` (wispOp setWifi)
