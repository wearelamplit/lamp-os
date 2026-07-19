# Changelog

Notable changes per firmware version. The version is the root `VERSION` file;
add an entry here in the **same change** that bumps it. Highlights grouped
Added / Fixed / Changed — not every commit.

## 1.1.1 — pending (bench + release)

### Added
- **Battery Saver** — a per-lamp brightness ceiling (Saver / Standard / Bright)
  that trades brightness for runtime, with a battery-life estimate from the
  lamp's measured draw.
- **ESP-NOW v2 mesh frames** — larger frames (up to ~1470 B vs the classic
  ~250 B) with per-target OTA chunk-size negotiation (200 B baseline → 1444 B
  ceiling, advertised via a HELLO TLV) and both-sided RSSI-gated offers, so OTA
  moves in fewer packets with less BLE-coex contention. Non-advertising peers
  fall back to the 200 B baseline.
- **OTA channel promotion** — a `-beta` lamp graduates to `-stable` over the air
  (no USB reflash), gated on top of the ed25519 verify.
- **Mesh command-auth** — HMAC-SHA256 tag on the "force another lamp" frames
  (EVENT/COMMAND); dev / from-source builds can't drive cascades or greetings on
  keyed lamps.
- **Social greeting base-gradient** — lamps exchange colors on greet
  (`MSG_COLOR_QUERY` / `MSG_COLOR_INFO`) and render the peer's base as a blended
  gradient. Cross-platform: iOS and Android lamps recognize each other via a
  self-reported BD_ADDR social identity.
- **Home mode redesign** — network binding is opt-in (plain on/off by default,
  no WiFi scan unless enabled); per-expression-type + social quiet toggles
  replace the hardcoded `allowedInHomeMode` flag.
- **Per-lamp LED byte-order** picker (RGB/GRB/…), end to end.
- **Web config UI overhaul** — AP-duration setup, password gate, expressions
  modal (duplicate types + preview-release), 5-tap advanced LED-type picker,
  home-mode toggles, adopt-on-save, `Features::WebApp` snafu gate.
- **Gamma-corrected LED output** — colors match the app instead of washing to
  white on the strips; tunable `kOutputGamma`.
- **Version reporting** — firmware version derived from the root `VERSION` file;
  the wisp broadcasts it and the app + web UI show it.
- **Dev firmware panel** (dev-channel app) — a mesh-lamps roster screen with a
  5 s poll for bench visibility into fleet state.

### Fixed
- **OTA re-offer loop** — reject unverifiable firmware offers up front; the
  distributor self-verifies at boot and won't offer if it can't. Cross-variant
  offers that a peer declines are now blocklisted instead of re-offered on a
  timer (kills the standard→snafu offer/reject churn).
- **Post-boot roster dead zone** — a fresh lamp sends HELLOs every 5 s for its
  first 30 s (then settles to the 60 s cadence), so it fills its neighbor roster
  quickly instead of waiting a full interval before cascades can reach it.
- **OTA offer authentication** — the firmware offer carries an auth tag.
- **Expression-editor ghost rows** — the Test preview no longer writes a
  transient config to NVS, so previewing doesn't leave duplicate rows behind.
- **Brownout on boot** with the full segment config.
- Full Salty **snub floors at ~10%** instead of full black.
- Config migration for the per-lamp LED type.

### Changed
- **Mesh HELLO cadence** slowed to 60 s (roster prune at 240 s), lamp + wisp —
  less idle mesh chatter now that the boot burst covers fast initial fill.
- **Cascade payload budget** raised from 224 B to the v2 frame ceiling (1444 B),
  so big-palette cascades send. The oversized broadcast reaches v2 peers only; a
  mixed-fleet capability gate is owed before public beta.
- **Per-frame supply-budget power governor** replaces the fixed brightness cap —
  brightness is scaled to a measured power budget each frame instead of a flat
  ceiling.
- **Expression rework:**
  - *Glitchy* — a `Coverage` slider (0–100%, always active) sets how much of the
    active region glitches each frame as single-pixel specks; the old
    `count`/points, `Size`, and `Grain` controls are gone and each frame repaints
    from the background, so coverage is a steady scatter density, not an
    accumulating solid.
  - *Shifty* — the dead interval control is dropped (continuous fill); Uniform
    fill honors the configured zone.
  - *Breathing* — reworked into a Sections wave: a soft-edge vignette (taper +
    curve), staggered random section order, 8 s breath floor.
  - *Spotty* — size is a Small–Large control (1–6 px), each spot gets a random
    lifetime band, and the fast end of the speed range flickers like a candle.
  - A **universal Whole-strip / Region zone toggle** across expressions via a
    shared `resolveZone()` helper; Pulse's size is now a percentage of the zone.
- **Nearby roster cap** raised to 50.
- Mesh wire-format core **extracted to a shared library** (lamp and wisp build
  from one source instead of drifting mirror copies).
- Local flash defaults to **unsigned**; signing is CI's job.
- Firmware tests exercise the **real code** (drifting mirror doubles removed).
- Comment-policy pass across the lamp + wisp firmware.
