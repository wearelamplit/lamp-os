# Design: Onboarding — Adopt-Confirm + Pulse-to-Identify

**Date:** 2026-06-28
**Surface:** `software/lamp-app-flutter` — the add-lamp (onboarding) flow
**Branch:** `ux-pages` (second per-page pass, on top of the Control-flow pass)

## Problem

The add-lamp flow has friction and mis-framed copy:

- The **"meet" step** (`add_lamp_meet_step.dart`) is a full screen *after* naming with no
  input — critter + a reassurance paragraph + "Sounds good." A wizard page that exists
  only to be dismissed, and it frames the lamp as already adopted ("They're yours to
  shape from here on") before it has been claimed.
- There's **no way to physically identify** which lamp you're adopting — relevant even
  when one lamp is around (confirm you've got the right one), and more so with several.
- The **verify view** keeps the password TextFields rendered during the ~8s post-claim
  wait (only the buttons disable), so you see editable-looking fields during a
  non-interactive wait.
- **Copy is mis-toned** — romantic phrasing ("First moves with {name}", "Welcome them
  home") rather than the product's cute, whimsical "adopting a stray" voice.

## Goal

Reframe onboarding around **adopting a stray lamp you can physically identify**: a
pulse-to-confirm step right after you pick a lamp (before naming), copy in a cute
whimsical stray-adoption voice, and a clean verify view.

## Connection constraint (load-bearing)

The app is **not connected** to any lamp during scan/name/password — the scan list is
BLE-advertisement scanning (`nearbyLampsNotifierProvider`), and holding a link during
form-fill trips `LINK_SUPERVISION_TIMEOUT`, so the only real connection is at
claim-submit. The pulse therefore opens a connection **only for the adopt-confirm step**
and closes it on leaving (either direction). The claim still connects fresh at submit —
the existing "deferred connect, no link held during form-fill" rule is preserved.

## Section 1 — Flow restructure + pulse-to-identify

**New step order:** `scan → [tap lamp → connect] → adopt-confirm (lamp pulsing) → name →
password → verifying → done`. The post-naming **"meet" step is removed**; the
adopt-confirmation takes its place **right after you pick a lamp**, before naming.

**Adopt-confirm step (new):**
- On entry: **connect** to the tapped lamp (factory-default unclaimed lamps accept
  control-service writes unauthenticated — same path the claim uses). Start the **pulse**.
- **Pulse mechanism (no firmware work):** reuse the existing expression-test trigger
  (`controlNotifier.testExpression` → `BleUuids.expressionTest`; the firmware already has
  a `pulse` expression). Build a pulse `ExpressionConfig` in a **washed-out-bright version
  of the lamp's advertised base color** (blend the base color toward white, full
  brightness — we already have the base color from the scan advertisement). Expressions are
  one-shot, so re-fire on a **~1.5–2s repeat timer** while the page is open so it keeps
  pulsing.
- Shows the lamp (its critter + colors) + the confirm copy (Section 2).
- Buttons: **Adopt** → stop pulse + disconnect, advance to **name**; **Cancel** → stop
  pulse + disconnect, return to scan list.
- **Cleanup must run on every exit path** (Adopt / Cancel / app backgrounded / dispose):
  send the empty `expressionTest` "stop" write + disconnect, so a lamp is never left
  stuck pulsing.
- **Connect/pulse failure** (lamp drifted out of range): inline "Couldn't reach it — move
  closer" with a Retry, plus Cancel to go back.

## Section 2 — Copy review (cute whimsical "stray adoption" voice)

Lean into "a stray lamp you're taking in / giving a home"; keep the critter warmth; drop
romantic phrasing. Headline strings:

| Where | Now | New |
|---|---|---|
| Adopt-confirm title | *(new)* | "Found your stray?" |
| Adopt-confirm body | *(new)* | "The one blinking at you is the stray you tapped. Take it in?" |
| Adopt-confirm buttons | *(new)* | "Adopt" / "Cancel" |
| Password submit | "Welcome them home" | "Take {name} home" |
| Verifying button | "Settling in…" | "Settling in…" (keep) |
| Done CTA | "Say hi to {name}" | "Say hi to {name}" (keep) |
| Done section header | "First moves with {name}" | "Getting to know {name}" |

The old "meet" reassurance paragraph is **dropped** (it was flow padding); optionally one
friendly line can live under the Done step's "Getting to know {name}". During
implementation, do a **full read of every onboarding string** and bring the complete
revised set for the owner to sign off — the table above is the headline set + the binding
tone (cute, whimsical, stray-adoption; no romantic phrasing; "Cancel" not "Keep looking",
since usually only one lamp is around).

## Section 3 — Verify-view cleanup

In `AddLampPasswordStep`, the `step == verifying` branch currently leaves the password
TextFields rendered. Change it to a **dedicated clean verify state**: the lamp's critter +
the existing rotating `_VerifyingTips` + "Settling in…", with the **password fields hidden
entirely** (no inputs during a non-interactive wait). Same `_VerifyingTips` content,
without the form behind it.

## Section 4 — Testing

- **Flow/state machine:** tap a lamp → adopt-confirm appears + a connection opens; **Adopt**
  → advances to `name` with the pulse stopped + disconnected; **Cancel** → returns to scan
  with the pulse stopped + disconnected. Assert via `InMemoryBleClient`: `expressionTest`
  written while on the page; the empty "stop" write + disconnect on BOTH exits.
- **Pulse:** `testExpression` fired with the washed-out-bright pulse config; the repeat
  timer re-fires (drive with `fake_async`); cleanup runs on every exit (Adopt / Cancel /
  dispose).
- **Verify view:** when `step == verifying`, the password fields are absent and
  `_VerifyingTips` + "Settling in…" render.
- **Copy:** the new strings render ("Found your stray?", "Take {name} home", "Getting to
  know {name}").
- Existing `add_lamp_notifier` / onboarding tests stay green; update any that asserted the
  old "meet" step or the old flow order.
- Run via root `npm run app:test` / `npm run app:analyze`; keep the suite green.

## Out of scope

- The connection/reconnect state machine for the claim (untouched — claim still connects
  fresh at submit).
- Any firmware change (the `pulse` expression + `expressionTest` trigger already exist).
- Other per-page passes (setup hub, social/expressions) — separate cycles.

## Notes for the implementer

- A washed-out-bright helper for the base color: blend toward white (~50%) at full
  brightness; reuse `LampColor`/existing color utils — don't add a new dependency.
- Pulse re-trigger should pause when the app backgrounds and clean up on dispose.
- The adopt-confirm connection is independent of the claim — open it on entry, close it on
  exit; do not hold it through `name`.
