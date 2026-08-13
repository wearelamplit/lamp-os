# Firmware proposal: byteOrder field on shade/base sections

**Status:** Proposed
**Date:** 2026-05-29
**Owner:** Jerrett

## Problem

The current firmware derives the NeoPixel byte order purely from the
section's `bpp` field in `standard_lamp.cpp`:
- `bpp == 3` → `NEO_GRB`
- `bpp == 4` → `NEO_GRBW`

Some art lamps have been built with strips whose hardware uses BGR
ordering. With the current logic those strips show wrong-coloured output
(red and blue swapped) and there is no way to correct it from the app.

The Flutter app's Advanced LED setup screen already has a placeholder
`InfoPanel` informing the user that BGR support is "coming soon" — this
proposal is the firmware-side change that unblocks the third
`SegmentedButton` option in `advanced_leds_screen.dart`.

## Proposed change

Add an independent `byteOrder` string field to the `shade` and `base`
section JSON in `settings_blob`:

| `byteOrder` | NeoPixel constant | `bpp` implied |
|-------------|--------------------|---------------|
| `"GRB"`     | `NEO_GRB`          | 3             |
| `"GRBW"`    | `NEO_GRBW`         | 4             |
| `"BGR"`     | `NEO_BGR`          | 3             |
| _(future)_  | `RGB`, `RGBW`, `BGRW` … |          |

`bpp` stays on the wire for backwards compatibility. Missing
`byteOrder` derives from `bpp`:

- absent or `bpp == 3` → `"GRB"`
- `bpp == 4`           → `"GRBW"`

This makes the migration invisible to existing devices: a lamp running
the new firmware reads its old settings, infers `byteOrder` from `bpp`,
and continues to behave exactly as before.

## Firmware touch-points

- `standard_lamp.cpp` near lines 394–395 — replace the `bpp`-based
  `shadeFmt`/`baseFmt` derivation with a switch on the new field.
- `settings_blob.cpp` — load/save the new field; default per the table
  above.
- The per-section read characteristics `shadeSection` / `baseSection`
  — emit `byteOrder` alongside the existing `bpp`.

## App-side follow-up

When the firmware ships, do the following in `advanced_leds_screen.dart`:

1. Expand the `SegmentedButton<int>` (currently 4/3 → RGBW/RGB) into a
   `SegmentedButton<String>` over `{GRBW, GRB, BGR}` driven by the new
   field.
2. Remove the "BGR is planned" `InfoPanel` (currently around lines
   108–115 of the screen).
3. Add `setBaseByteOrder(String)` / `setShadeByteOrder(String)` mutators
   to `ControlNotifier` that follow the same state-only-mutator pattern
   the `*Bpp` and `*Px` mutators already use.

The app changes are out of scope for this plan; the BGR option in the
UI stays disabled until the firmware lands.

## Backwards compatibility

- Old firmware on the wire: no `byteOrder` field is emitted; the app
  ignores it and falls back to deriving from `bpp`. The third UI option
  stays hidden.
- New firmware + old app: new field is ignored by the app; behavior
  unchanged.
- New firmware + new app: full three-way segmented button works.

No migration script is required. No NVS schema bump is required.

## Open questions

- Should we also accept `RGB` as an alias for the same byte order as
  `GRB` (some commodity strips ship that way)? Punt unless a user
  reports a strip that needs it.
- Adafruit_NeoPixel supports `NEO_BGRW`. Add when a user requests it.
