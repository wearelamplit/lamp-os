# Architecture Decision Records

ADRs capture the **significant, hard-to-reverse decisions** that shape this
system (the current C++/ESP32 lamp firmware), especially where it diverges from
the old `main`-branch system. Each records the *context/forces*, the *decision*,
the *alternatives rejected*, and the *consequences we live with* — so future
changes start from "why was it this way," not a blank slate.

Format: one file per decision, numbered `NNNN-short-title.md`. Status is
`Accepted` / `Superseded by NNNN` / `Proposed`. Keep them short and honest about
the costs, not just the wins.

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-esp-now-mesh-networking.md) | ESP-NOW broadcast mesh for lamp-to-lamp networking | Accepted |
| [0002](0002-ota-over-mesh.md) | Firmware OTA over the ESP-NOW mesh (signed gossip + upfront-erase) | Accepted |
| [0003](0003-dual-core-concurrency-model.md) | Dual-core concurrency model (Core 0 radio / Core 1 loop) | Accepted |

## Candidate ADRs (not yet written — pick which are worth capturing)

Decisions that also distinguish this system from `main` and may deserve an ADR:

- **Protocol versioning** — the EMIT/RX *range* split + TLV-first additive
  evolution (subtle, load-bearing during mixed-version OTA waves). *(top pick)*
- **Firmware signing + `{type}-{channel}` OTA gating** — LSIG footer, ed25519,
  per-variant gating. *(top pick)*
- **Frozen BLE GATT layout** — append-only, no Service Changed, the Android
  handle-staleness constraint behind the freeze.
- **Variant architecture** — `standard`/`snafu` via injected `HwConfig`,
  framework-vs-variant include hygiene.

(0001/0002/0003 were the first three confirmed. The protocol-versioning and
signing/gating ones are the strongest remaining candidates — they're the subtle
decisions future maintainers are most likely to break by accident.)
