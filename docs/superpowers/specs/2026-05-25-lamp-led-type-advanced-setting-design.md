# Lamp LED Type Advanced Setting

## Context

Both NeoPixel strips in the lamp firmware are currently hardcoded to
`NEO_GRBW` (4 bpp, SK6812 RGBW). Some lamps in the field have RGB-only
strips (WS2812B, 3 bpp), and they're served wrong-colored output because
the firmware writes a white-channel byte the strip ignores, leaving the
intended color misaligned.

The fix needs a runtime config field for each strip's LED type, exposed
in the web UI behind an "advanced" gate so the average lamp owner doesn't
see (or accidentally change) it. The unlock is a tap-5-times gesture on
the LamplitLogo on the Info page — once enabled, it persists in NVS so
it survives reboots. No banner, no toast: tapping just routes to Setup
where the extra fields are now visible.

## Components

### Firmware

**`software/lamp-os/src/config/config_types.hpp`** — three new fields:

```cpp
class LampSettings {
 public:
  // ... existing fields
  bool advancedEnabled = false;
};

class ShadeSettings {
 public:
  uint8_t px = 38;
  uint8_t bpp = 4;        // NEW: 4 = NEO_GRBW (RGBWW), 3 = NEO_GRB (RGB)
  std::vector<Color> colors = {Color(0x00, 0x00, 0x00, 0xFF)};
};

class BaseSettings {
 public:
  uint8_t px = 35;
  uint8_t bpp = 4;        // NEW
  // ... existing fields
};
```

**`software/lamp-os/src/config/config.cpp`** — serialize/deserialize the
new fields in both `Config(Preferences*)` (load from NVS) and
`asJsonDocument()` (send to UI). Defaults of `4` and `false` ensure
backward compatibility with existing lamp configs that lack these keys.

**`software/lamp-os/src/lamps/standard_lamp.cpp`** — change static
NeoPixel globals to pointers, construct in `setup()` based on config:

```cpp
// File scope:
Adafruit_NeoPixel* shadeStrip = nullptr;
Adafruit_NeoPixel* baseStrip = nullptr;

// In setup(), after config loads:
const uint16_t shadeFmt = (config.shade.bpp == 3) ? NEO_GRB : NEO_GRBW;
const uint16_t baseFmt  = (config.base.bpp  == 3) ? NEO_GRB : NEO_GRBW;
shadeStrip = new Adafruit_NeoPixel(LAMP_MAX_STRIP_PIXELS_SHADE, LAMP_SHADE_PIN, shadeFmt + NEO_KHZ800);
baseStrip  = new Adafruit_NeoPixel(LAMP_MAX_STRIP_PIXELS_BASE,  LAMP_BASE_PIN,  baseFmt  + NEO_KHZ800);
shadeStrip->begin();
baseStrip->begin();
```

All call sites change from `shadeStrip.method()` to `shadeStrip->method()`
(grep + mechanical edit). Same for `baseStrip` and any pass-by-reference
sites — `FrameBuffer::begin(...)` already takes `Adafruit_NeoPixel*` per
its existing signature, so callers already pass `&shadeStrip`; those
become `shadeStrip` (already a pointer).

**Reboot semantics**: changing `bpp` requires reconstructing the NeoPixel
object, which only happens in `setup()`. The existing `/settings PUT`
handler in `wifi.cpp` already sets `requiresReboot = true` when any
config field changes — no new wiring needed.

### Frontend

**`software/lamp-ui/src/stores/lamp.ts`** — extend state to mirror the
new firmware fields, and add setter actions:

```ts
// state
lamp: {
  // existing fields...
  advancedEnabled: false,
},
shade: { px, colors, bpp: 4 },
base:  { px, colors, knockoutPixels, ac, bpp: 4 },

// actions
updateAdvancedEnabled(value: boolean)
updateShadeBpp(value: number)
updateBaseBpp(value: number)
```

Each action sets the local state and triggers the existing save-to-NVS
flow (PUT `/settings`).

**`software/lamp-ui/src/composables/useTapCounter.ts`** (new) — generic
N-taps-within-window detector:

```ts
export function useTapCounter(
  count: number,
  windowMs: number,
  onTriggered: () => void,
) {
  const taps = ref<number[]>([])
  function recordTap() {
    const now = Date.now()
    taps.value = [...taps.value.filter(t => now - t < windowMs), now]
    if (taps.value.length >= count) {
      taps.value = []
      onTriggered()
    }
  }
  return { recordTap }
}
```

Sliding-window: each tap evicts taps older than `windowMs`, then appends
the current tap. When the buffer hits `count`, fire and reset. Pauses
longer than `windowMs` reset the count naturally.

**`software/lamp-ui/src/pages/Info.vue`** — bind the logo to the
composable:

```vue
<script setup lang="ts">
import { useRouter } from 'vue-router'
import { useTapCounter } from '@/composables/useTapCounter'
import { useLampStore } from '@/stores/lamp'

const router = useRouter()
const lampStore = useLampStore()

const { recordTap } = useTapCounter(5, 3000, () => {
  if (lampStore.lamp.advancedEnabled) return  // already on, nothing to do
  lampStore.updateAdvancedEnabled(true)
  router.push('/setup')
})
</script>

<template>
  <!-- existing structure unchanged, just wrap logo in clickable -->
  <div class="logo-container" @click="recordTap" style="cursor: default;">
    <LamplitLogo style="width: 50%; max-width: 200px;" />
  </div>
  <!-- rest unchanged -->
</template>
```

No visible affordance (no pointer cursor change, no hover state) — the
gesture is intentionally undiscoverable for non-power-users.

**`software/lamp-ui/src/components/Form.vue`** — register a new `select`
field type. Renders a native `<select>` element styled to match the
existing form fields (compact, full-width, matches other field padding).
The dynamic field schema gains:

```ts
type SelectField = {
  name: string
  type: 'select'
  label: string
  default: number | string
  optional?: boolean
  options: Array<{ value: number | string; label: string }>
}
```

The renderer emits the selected `value` (not the label) via the same
`@update:model-value` pattern as other fields.

**`software/lamp-ui/src/pages/Setup.vue`** — add conditional fields,
each inside its strip's existing profile group, above the existing pixel
count:

```ts
// Inside the fields array, conditional on advancedEnabled
...(lampStore.lamp.advancedEnabled ? [{
  name: 'shadeBpp',
  type: 'select' as const,
  label: 'Shade LED Type',
  default: 4,
  optional: true,
  options: [{ value: 4, label: 'RGBWW' }, { value: 3, label: 'RGB' }],
}] : []),
// existing shade fields (shadeHeading, shadePx, etc.)
```

Same pattern for `baseBpp` in the base group above `basePx`. When
`advancedEnabled` is false, the spread evaluates to an empty array and
the field is absent — no DOM, no space cost.

## Files modified

- `software/lamp-os/src/config/config_types.hpp` — add `advancedEnabled` to LampSettings, `bpp` to BaseSettings + ShadeSettings
- `software/lamp-os/src/config/config.cpp` — load/save the new fields
- `software/lamp-os/src/lamps/standard_lamp.cpp` — convert strips to pointers, construct from config in `setup()`, update all `.` → `->` usages

(`wifi.cpp` needs no edit: `/settings GET` already returns `config.asJsonDocument()` which will include the new fields automatically once they're serialized in `config.cpp`.)

- `software/lamp-ui/src/stores/lamp.ts` — add state + actions for `advancedEnabled`, `shade.bpp`, `base.bpp`
- `software/lamp-ui/src/composables/useTapCounter.ts` — NEW
- `software/lamp-ui/src/pages/Info.vue` — wrap logo in tap handler
- `software/lamp-ui/src/components/Form.vue` — add `select` field type renderer
- `software/lamp-ui/src/pages/Setup.vue` — add the two conditional fields

## Verification

Manual end-to-end on hardware (no automated tests for this):

1. **Build clean**: `cd software/lamp-ui && npm run build` succeeds, no TS errors.
2. **Flash firmware + filesystem**: `npm run build-and-flash`. Boot the lamp.
3. **Existing config still loads**: open `/` on the lamp. UI renders. All existing settings (brightness, colors, expressions) are intact — defaulting `advancedEnabled=false` and `bpp=4` doesn't disturb existing NVS keys.
4. **Advanced is hidden by default**: navigate to Setup tab. The two new fields are absent. The form is visually unchanged from before.
5. **Unlock gesture**: navigate to Info tab. Tap the logo 5 times within 3 seconds. The app navigates to Setup; the new "Shade LED Type" and "Base LED Type" select rows are now visible above each strip's pixel-count field. No banner, no message, no animation — just the new fields.
6. **Persistence across reload**: hard-refresh the page. The new fields stay visible (because `advancedEnabled` was PUT to NVS and the fresh `/settings` GET returns it true).
7. **bpp change triggers reboot prompt**: change Shade LED Type from RGBWW to RGB and save. The existing "save → reboot required" UI fires (since `/settings PUT` already sets `requiresReboot`).
8. **Strip actually re-inits at the new bpp**: after reboot, light up the shade with a known pure color (e.g. set base color to pure white `#FFFFFF`). On an RGB-only strip with `bpp=3` configured, white should appear as `R=255, G=255, B=255` (warm-toned). With `bpp=4` mis-configured on the same RGB strip, every 4th LED was wrong-colored — that artifact should be gone. Test on a lamp that has the right hardware (RGB vs RGBWW strip).
9. **Tap gesture is non-discoverable**: with `advancedEnabled` already on, tapping the logo does nothing visible (the composable runs the early-return path). Verifies no UX leakage.

If any step fails, the firmware-side changes can be reverted independently of the frontend changes (and vice versa) since the JSON contract is backward-compatible.
