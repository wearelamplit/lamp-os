# Lamp LED Type Advanced Setting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-strip RGB/RGBWW configuration to the lamp, hidden behind a 5-tap-on-logo "advanced settings" gate.

**Architecture:** Firmware adds `bpp` fields (3 or 4 bytes-per-pixel) to `ShadeSettings`/`BaseSettings` and `advancedEnabled` to `LampSettings`, all persisted in NVS. NeoPixel strip globals become pointers initialized in `setup()` from config. Frontend adds a new `select` field type to the dynamic form renderer, a `useTapCounter` composable, and conditional fields on the Setup page that appear when `advancedEnabled` is true.

**Tech Stack:** PlatformIO + Arduino + ESPAsyncWebServer + Adafruit NeoPixel (firmware); Vue 3 + Pinia + Vue Router + Vite (frontend).

**Working branch:** `beta` (or a feature branch off `beta`). Spec lives at `docs/superpowers/specs/2026-05-25-lamp-led-type-advanced-setting-design.md`.

---

## Task 1: Firmware config — add `bpp` and `advancedEnabled` fields

**Files:**
- Modify: `software/lamp-os/src/config/config_types.hpp` (LampSettings, ShadeSettings, BaseSettings classes)

- [ ] **Step 1: Read the current LampSettings, ShadeSettings, BaseSettings classes**

Run:
```bash
sed -n '37,80p' software/lamp-os/src/config/config_types.hpp
```

Expected: see existing classes `LampSettings { name, brightness, homeMode, homeModeSSID, homeModeBrightness, password }`, `ShadeSettings { px, colors }`, `BaseSettings { px, colors, knockoutPixels, ac }`.

- [ ] **Step 2: Add `advancedEnabled` to `LampSettings`**

In `software/lamp-os/src/config/config_types.hpp`, add a new field at the end of the `LampSettings` public members:

```cpp
class LampSettings {
 public:
  std::string name = "standard";
  uint8_t brightness = 100;
  bool homeMode = false;
  std::string homeModeSSID = "";
  uint8_t homeModeBrightness = 80;
  std::string password = "";
  bool advancedEnabled = false;   // NEW
};
```

(Use the actual existing fields — don't duplicate or remove anything. Just add `advancedEnabled`.)

- [ ] **Step 3: Add `bpp` to `ShadeSettings`**

In the `ShadeSettings` class:

```cpp
class ShadeSettings {
 public:
  uint8_t px = 38;
  uint8_t bpp = 4;   // NEW: 4 = NEO_GRBW (RGBWW), 3 = NEO_GRB (RGB)
  std::vector<Color> colors = {Color(0x00, 0x00, 0x00, 0xFF)};
};
```

- [ ] **Step 4: Add `bpp` to `BaseSettings`**

In the `BaseSettings` class:

```cpp
class BaseSettings {
 public:
  uint8_t px = 35;
  uint8_t bpp = 4;   // NEW
  std::vector<Color> colors = {Color(0x30, 0x07, 0x83, 0x00)};
  std::vector<uint8_t> knockoutPixels = std::vector<uint8_t>(50, (uint8_t)100);
  uint8_t ac = 0;
};
```

- [ ] **Step 5: Build to confirm no compile errors**

Run:
```bash
cd software/lamp-os
pio run -e upesy_wroom
```

Expected: SUCCESS. The new fields default to `false`/`4` so existing code that doesn't touch them still compiles and behaves identically.

- [ ] **Step 6: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-os/src/config/config_types.hpp
git commit -m "config: add advancedEnabled and per-strip bpp fields"
```

---

## Task 2: Firmware config — serialize new fields to/from JSON

**Files:**
- Modify: `software/lamp-os/src/config/config.cpp` (`Config(Preferences*)` constructor and `asJsonDocument()`)

- [ ] **Step 1: Deserialize `advancedEnabled` from JSON in the constructor**

In `software/lamp-os/src/config/config.cpp`, find the block in `Config(Preferences*)` that reads `JsonObject lampNode = doc["lamp"];` and the following lines that set `lamp.name`, `lamp.brightness`, etc. Add one line at the end of that block:

```cpp
  lamp.advancedEnabled = lampNode["advancedEnabled"] | false;
```

(The `|` operator in ArduinoJson provides the default when the key is missing or wrong type. `false` matches the class-default and keeps existing configs that lack this key working.)

- [ ] **Step 2: Deserialize `base.bpp` from JSON**

In the same constructor, find `JsonObject baseNode = doc["base"];` and the lines that set `base.px`, `base.ac`. Add:

```cpp
  base.bpp = baseNode["bpp"] | 4;
  if (base.bpp != 3 && base.bpp != 4) {
    base.bpp = 4;  // defensive: only 3 or 4 valid
  }
```

- [ ] **Step 3: Deserialize `shade.bpp` from JSON**

In the same constructor, find `JsonObject shadeNode = doc["shade"];`. Add:

```cpp
  shade.bpp = shadeNode["bpp"] | 4;
  if (shade.bpp != 3 && shade.bpp != 4) {
    shade.bpp = 4;
  }
```

- [ ] **Step 4: Serialize `lamp.advancedEnabled` in `asJsonDocument()`**

In `Config::asJsonDocument()`, find the block setting `lampNode["name"]`, `lampNode["brightness"]`, etc. Add at the end:

```cpp
  lampNode["advancedEnabled"] = lamp.advancedEnabled;
```

- [ ] **Step 5: Serialize `base.bpp` in `asJsonDocument()`**

In the block setting `baseNode["px"]`, `baseNode["ac"]`:

```cpp
  baseNode["bpp"] = base.bpp;
```

- [ ] **Step 6: Serialize `shade.bpp` in `asJsonDocument()`**

In the block setting `shadeNode["px"]`:

```cpp
  shadeNode["bpp"] = shade.bpp;
```

- [ ] **Step 7: Build to confirm no compile errors**

```bash
cd software/lamp-os
pio run -e upesy_wroom
```

Expected: SUCCESS.

- [ ] **Step 8: Flash firmware + filesystem to test**

```bash
cd /Users/jerrett/projects/lamp-os
npm run build-and-flash
```

Expected: flash completes successfully, lamp boots.

- [ ] **Step 9: Verify the new fields appear in the GET /settings JSON**

From a device on the lamp's AP (e.g. phone on lamp WiFi), or from a laptop on the lamp's home network, run:

```bash
curl -s http://192.168.4.1/settings | python3 -m json.tool | head -30
```

(Or use the lamp's home-network IP / `lamp.local` if applicable.)

Expected output contains:
```json
{
  "lamp": {
    ...,
    "advancedEnabled": false
  },
  "base": {
    "px": 36,
    "bpp": 4,
    ...
  },
  "shade": {
    "bpp": 4,
    ...
  }
}
```

- [ ] **Step 10: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-os/src/config/config.cpp
git commit -m "config: serialize advancedEnabled and bpp fields"
```

---

## Task 3: Firmware — make NeoPixel strips configurable at runtime

**Files:**
- Modify: `software/lamp-os/src/lamps/standard_lamp.cpp` (NeoPixel global declarations + setup + all call sites)

- [ ] **Step 1: Find all references to `shadeStrip` and `baseStrip`**

Run:
```bash
cd /Users/jerrett/projects/lamp-os
grep -n "shadeStrip\|baseStrip" software/lamp-os/src/lamps/standard_lamp.cpp
```

Expected: see the two declarations at the top (currently `Adafruit_NeoPixel shadeStrip(...)`, `Adafruit_NeoPixel baseStrip(...)`) plus all call sites that use `.method()` (e.g. `.begin()`, `.show()`, `.setPixelColor()`, etc.). Note them — you'll need to convert each from `.` to `->`.

- [ ] **Step 2: Change the global declarations to pointers**

In `software/lamp-os/src/lamps/standard_lamp.cpp`, replace the existing lines:

```cpp
Adafruit_NeoPixel shadeStrip(LAMP_MAX_STRIP_PIXELS_SHADE, LAMP_SHADE_PIN, NEO_GRBW + NEO_KHZ800);
Adafruit_NeoPixel baseStrip(LAMP_MAX_STRIP_PIXELS_BASE, LAMP_BASE_PIN, NEO_GRBW + NEO_KHZ800);
```

with:

```cpp
Adafruit_NeoPixel* shadeStrip = nullptr;
Adafruit_NeoPixel* baseStrip = nullptr;
```

- [ ] **Step 3: Locate the `setup()` function in `standard_lamp.cpp`**

Run:
```bash
grep -n "void setup\|^setup\(" software/lamp-os/src/lamps/standard_lamp.cpp
```

Find the `setup()` function (or whichever init function loads config — check what runs `Config()` or similar). The strips need to be constructed AFTER config loads but BEFORE anything calls `shadeStrip->begin()` or uses the strips.

- [ ] **Step 4: Construct the strips from config in `setup()`**

In `setup()`, AFTER the config object is constructed/loaded and BEFORE any code that uses the strips, add:

```cpp
const uint16_t shadeFmt = (config.shade.bpp == 3) ? NEO_GRB : NEO_GRBW;
const uint16_t baseFmt  = (config.base.bpp  == 3) ? NEO_GRB : NEO_GRBW;
shadeStrip = new Adafruit_NeoPixel(LAMP_MAX_STRIP_PIXELS_SHADE, LAMP_SHADE_PIN, shadeFmt + NEO_KHZ800);
baseStrip  = new Adafruit_NeoPixel(LAMP_MAX_STRIP_PIXELS_BASE,  LAMP_BASE_PIN,  baseFmt  + NEO_KHZ800);
shadeStrip->begin();
baseStrip->begin();
```

(If the existing code calls `.begin()` somewhere else, remove those calls — we're handling begin here.)

- [ ] **Step 5: Convert every other `shadeStrip.X` and `baseStrip.X` call site to `->`**

Mechanical edit. For each line that uses `shadeStrip.something()` or `baseStrip.something()`, change `.` to `->`. Don't change references that PASS the strip (e.g. `&shadeStrip` becomes just `shadeStrip` since it's already a pointer).

Search to confirm none missed:
```bash
grep -n "shadeStrip\.\|baseStrip\." software/lamp-os/src/lamps/standard_lamp.cpp
```

Expected: no matches (every usage is now `->`).

- [ ] **Step 6: Check for pass-by-reference call sites that need updating**

The `FrameBuffer::begin(...)` signature takes `Adafruit_NeoPixel*` per `software/lamp-os/src/core/frame_buffer.hpp`. Existing code that did `shade.begin(... &shadeStrip);` needs to become `shade.begin(... shadeStrip);` since the variable is now already a pointer.

Search:
```bash
grep -n "&shadeStrip\|&baseStrip" software/lamp-os/src/lamps/standard_lamp.cpp
```

For any match, remove the `&` (the variable is already a pointer).

- [ ] **Step 7: Build to confirm no compile errors**

```bash
cd software/lamp-os
pio run -e upesy_wroom
```

Expected: SUCCESS. If type errors appear (e.g. "no member of type Adafruit_NeoPixel*"), there's a `.` you missed in Step 5 — re-grep and fix.

- [ ] **Step 8: Flash firmware only (data unchanged)**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-os
pio run -e upesy_wroom -t upload
```

Expected: SUCCESS.

- [ ] **Step 9: Boot the lamp and verify it still works with defaults**

Power-cycle or watch serial monitor:
```bash
pio device monitor -e upesy_wroom
```

Expected: lamp boots normally. With default `bpp=4` (NEO_GRBW), behavior is identical to before this change — colors look right, idle behavior runs, WiFi comes up, web UI is reachable.

Exit serial monitor (Ctrl-C, or in PIO's monitor: Ctrl-T then Q).

- [ ] **Step 10: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-os/src/lamps/standard_lamp.cpp
git commit -m "lamps: construct NeoPixel strips from config-driven bpp"
```

---

## Task 4: Frontend — extend lamp store with new state and actions

**Files:**
- Modify: `software/lamp-ui/src/stores/lamp.ts` (types + state + actions)

- [ ] **Step 1: Add `advancedEnabled` to the `LampSettings` interface**

In `software/lamp-ui/src/stores/lamp.ts`, find the `interface LampSettings` block and add:

```ts
interface LampSettings {
  name?: string
  brightness?: number
  homeMode?: boolean
  homeModeSSID?: string
  homeModeBrightness?: number
  password?: string
  advancedEnabled?: boolean   // NEW
}
```

- [ ] **Step 2: Add `bpp` to `ShadeSettings` and `BaseSettings` interfaces**

```ts
interface ShadeSettings {
  px?: number
  colors?: string[]
  bpp?: number   // NEW: 3 or 4
}

interface BaseSettings {
  px?: number
  colors?: string[]
  ac?: number
  knockout?: KnockoutPixel[]
  bpp?: number   // NEW
}
```

- [ ] **Step 3: Add `updateAdvancedEnabled` action**

Inside the `useLampStore = defineStore('lamp', () => { ... })` callback, alongside the existing update actions (search for `function updateLampName` or similar to find the cluster), add:

```ts
function updateAdvancedEnabled(enabled: boolean) {
  if (!state.value.lamp) state.value.lamp = {}
  state.value.lamp.advancedEnabled = enabled
}
```

- [ ] **Step 4: Add `updateShadeBpp` action**

```ts
function updateShadeBpp(bpp: number) {
  if (!state.value.shade) state.value.shade = {}
  state.value.shade.bpp = bpp
}
```

- [ ] **Step 5: Add `updateBaseBpp` action**

```ts
function updateBaseBpp(bpp: number) {
  if (!state.value.base) state.value.base = {}
  state.value.base.bpp = bpp
}
```

- [ ] **Step 6: Export the new actions from the store's return statement**

Find the `return { ... }` at the end of the store factory. Add the three new actions alongside the existing exports:

```ts
return {
  // ... existing exports
  updateAdvancedEnabled,
  updateShadeBpp,
  updateBaseBpp,
}
```

- [ ] **Step 7: Type-check passes**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-ui
npm run type-check
```

Expected: passes with no errors.

- [ ] **Step 8: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-ui/src/stores/lamp.ts
git commit -m "lamp store: add advancedEnabled and bpp state + actions"
```

---

## Task 5: Frontend — `useTapCounter` composable

**Files:**
- Create: `software/lamp-ui/src/composables/useTapCounter.ts`

- [ ] **Step 1: Check if `composables/` directory exists**

```bash
cd /Users/jerrett/projects/lamp-os
ls software/lamp-ui/src/composables/ 2>/dev/null || echo "doesn't exist, will create"
```

If it doesn't exist, `mkdir -p software/lamp-ui/src/composables/`.

- [ ] **Step 2: Create `useTapCounter.ts` with the sliding-window implementation**

Write `software/lamp-ui/src/composables/useTapCounter.ts`:

```ts
import { ref } from 'vue'

/**
 * Counts taps in a sliding time window and fires a callback when
 * `count` taps land within `windowMs` of each other. Designed for
 * "secret tap to unlock" gestures.
 *
 * @param count Number of taps required to trigger.
 * @param windowMs Time window for a continuous tap streak.
 * @param onTriggered Called once when the streak hits `count`.
 */
export function useTapCounter(
  count: number,
  windowMs: number,
  onTriggered: () => void,
) {
  const taps = ref<number[]>([])

  function recordTap() {
    const now = Date.now()
    taps.value = [...taps.value.filter((t) => now - t < windowMs), now]
    if (taps.value.length >= count) {
      taps.value = []
      onTriggered()
    }
  }

  return { recordTap }
}
```

- [ ] **Step 3: Type-check passes**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-ui
npm run type-check
```

Expected: passes.

- [ ] **Step 4: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-ui/src/composables/useTapCounter.ts
git commit -m "composables: useTapCounter for secret tap-N-times gestures"
```

---

## Task 6: Frontend — add `select` field type to dynamic form

**Files:**
- Create: `software/lamp-ui/src/components/fields/Select.vue`
- Modify: `software/lamp-ui/src/plugins/globalComponents.ts` (register new field)
- Modify: `software/lamp-ui/src/types.ts` (add `'select'` to `FieldType` union, and `options` to `FieldDefinition` if not present)

- [ ] **Step 1: Read an existing simple field (e.g. Boolean.vue) for structure**

```bash
sed -n '1,60p' software/lamp-ui/src/components/fields/Boolean.vue
```

Note the script setup, props/emits pattern, `validate()` method exposed.

- [ ] **Step 2: Create `Select.vue` matching the existing field component pattern**

Write `software/lamp-ui/src/components/fields/Select.vue`:

```vue
<script setup lang="ts">
import type { FieldValidationResult } from '@/types'

interface SelectOption {
  value: number | string
  label: string
}

interface Props {
  modelValue: number | string
  options: SelectOption[]
  disabled?: boolean
  required?: boolean
}

const props = withDefaults(defineProps<Props>(), {
  disabled: false,
  required: false,
})

const emit = defineEmits<{
  'update:modelValue': [value: number | string]
}>()

function onChange(event: Event) {
  const target = event.target as HTMLSelectElement
  // Match value back to its original type (number stayed number, string stayed string)
  const selected = props.options.find((o) => String(o.value) === target.value)
  if (selected) {
    emit('update:modelValue', selected.value)
  }
}

const validate = (): FieldValidationResult => {
  // A select with options always has a value (the default or chosen one)
  return { valid: true }
}

defineExpose({ validate })
</script>

<template>
  <select
    class="select-input"
    :value="String(modelValue)"
    :disabled="disabled"
    :required="required"
    @change="onChange"
  >
    <option
      v-for="opt in options"
      :key="String(opt.value)"
      :value="String(opt.value)"
    >
      {{ opt.label }}
    </option>
  </select>
</template>

<style scoped>
.select-input {
  width: 100%;
  padding: 0.5rem;
  border-radius: 4px;
  border: 1px solid var(--brand-cloud-grey, #e0e0e0);
  background: var(--brand-lamp-white, #fdfdfd);
  color: var(--brand-midnight-black, #0d0d0d);
  font-family: inherit;
  font-size: 1rem;
}
.select-input:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}
</style>
```

(The styling uses CSS variables from the existing brand palette; if names don't match exactly to other field components, copy the convention from `Boolean.vue` or `Text.vue` to be consistent.)

- [ ] **Step 3: Register `Select.vue` in `globalComponents.ts`**

Open `software/lamp-ui/src/plugins/globalComponents.ts` and:

1. Add an import:
   ```ts
   import SelectField from '@/components/fields/Select.vue'
   ```
2. Add to the `fieldComponents` map:
   ```ts
   'select-field': SelectField,
   ```

The map should now include `'select-field': SelectField` alongside `'boolean-field'`, `'text-field'`, etc.

- [ ] **Step 4: Add `'select'` to the `FieldType` union in `types.ts`**

In `software/lamp-ui/src/types.ts`, find the `FieldType` union and add `'select'`:

```ts
export type FieldType =
  | 'boolean'
  | 'text'
  | 'password'
  | 'number'
  | 'number-slider'
  | 'number-range-slider'
  | 'brightness-slider'
  | 'hidden'
  | 'color'
  | 'color-list'
  | 'slot'
  | 'group-heading'
  | 'select'   // NEW
```

- [ ] **Step 5: Ensure `FieldDefinition` allows an `options` array for select fields**

Check `types.ts` for `FieldDefinition` interface. If `options` isn't present, add it:

```ts
export interface FieldDefinition {
  // ... existing fields
  options?: Array<{ value: number | string; label: string }>
}
```

(If it's already there or if `FieldProps` covers it differently, no change needed.)

- [ ] **Step 6: Pass `options` to the field component in `Form.vue`**

In `software/lamp-ui/src/components/Form.vue`, find the `<component>` element that renders field types (around line 444 — the one inside the `<template v-else-if="field.name">` block) and ensure the `options` prop is passed when present.

Look at the current props passing:
```html
<component
  :is="getComponentName(field.type)"
  ...
  v-bind="field.props"
  :disabled="..."
  :required="..."
/>
```

The `field.props` spreads field props — but `options` lives on `field.options` (a top-level field property, not nested under props). Add an explicit bind:

```html
<component
  :is="getComponentName(field.type)"
  ...
  v-bind="field.props"
  :options="field.options"
  :disabled="..."
  :required="..."
/>
```

(If `options` is already wired via `field.props`, skip this; otherwise add the explicit binding so a select gets its options.)

- [ ] **Step 7: Type-check passes**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-ui
npm run type-check
```

Expected: passes.

- [ ] **Step 8: Build the UI to confirm no template errors**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-ui
npm run build
```

Expected: build succeeds. (We're not flashing yet — just confirming the build works.)

- [ ] **Step 9: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-ui/src/components/fields/Select.vue \
        software/lamp-ui/src/plugins/globalComponents.ts \
        software/lamp-ui/src/types.ts \
        software/lamp-ui/src/components/Form.vue
git commit -m "form: add select field type for dropdown inputs"
```

---

## Task 7: Frontend — wire tap-to-unlock on Info page

**Files:**
- Modify: `software/lamp-ui/src/pages/Info.vue` (add tap handler on logo, persist to store, navigate to setup)

- [ ] **Step 1: Read current Info.vue script and template structure**

```bash
cat software/lamp-ui/src/pages/Info.vue
```

Note: the logo is rendered as `<LamplitLogo style="width: 50%; max-width: 200px;" />` inside a `<div class="logo-container">`.

- [ ] **Step 2: Add imports and tap handler to `<script setup>`**

In `software/lamp-ui/src/pages/Info.vue`, the existing `<script setup lang="ts">` block has just `import LamplitLogo from '@/components/LamplitLogo.vue'`. Replace it with:

```vue
<script setup lang="ts">
import { useRouter } from 'vue-router'
import LamplitLogo from '@/components/LamplitLogo.vue'
import { useTapCounter } from '@/composables/useTapCounter'
import { useLampStore } from '@/stores/lamp'

const router = useRouter()
const lampStore = useLampStore()

const { recordTap } = useTapCounter(5, 3000, () => {
  // Idempotent: if already enabled, do nothing
  if (lampStore.state.lamp?.advancedEnabled) return
  lampStore.updateAdvancedEnabled(true)
  router.push('/setup')
})
</script>
```

(The store exposes `state` directly — verified at `software/lamp-ui/src/stores/lamp.ts` `return { ... }` and in existing usages like `lampStore.state.lamp?.name` in Setup.vue's `formValues`.)

- [ ] **Step 3: Wire the click handler on the logo container**

In the template, change:

```vue
<div class="logo-container">
  <LamplitLogo style="width: 50%; max-width: 200px;" />
</div>
```

to:

```vue
<div class="logo-container" @click="recordTap">
  <LamplitLogo style="width: 50%; max-width: 200px;" />
</div>
```

(No cursor change, no hover state, no visual affordance — the gesture is deliberately undiscoverable.)

- [ ] **Step 4: Type-check passes**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-ui
npm run type-check
```

Expected: passes. If it errors on `lampStore.state?.lamp?.advancedEnabled`, the store likely returns the `state` ref under a different property name — check the existing store export pattern (e.g. `loaded`, `saving`) and adapt the access.

- [ ] **Step 5: Build the UI**

```bash
npm run build
```

Expected: succeeds.

- [ ] **Step 6: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-ui/src/pages/Info.vue
git commit -m "info: 5-tap logo gesture unlocks advanced settings"
```

---

## Task 8: Frontend — add conditional LED Type fields on Setup page

**Files:**
- Modify: `software/lamp-ui/src/pages/Setup.vue` (insert conditional fields above each strip's pixel-count field)

Confirmed file shape: Setup.vue currently has only the **base** profile group (`ledProfileHeading` + `basePx` + `knockoutPixels` slot). No shade profile group exists. This task adds both a new shade group and the per-strip `bpp` fields, all conditional on `advancedEnabled`.

- [ ] **Step 1: Convert `fields` from `ref` to `computed`**

In `software/lamp-ui/src/pages/Setup.vue`, replace:

```ts
const fields = ref<FieldDefinition[]>([
  // ... existing array
])
```

with:

```ts
const fields = computed<FieldDefinition[]>(() => [
  // ... same array, now inside the arrow function
])
```

(`computed` is already imported via `import { ref, computed } from 'vue'`.)

- [ ] **Step 2: Insert `baseBpp` immediately before `basePx`**

Inside the computed array, find:

```ts
{
  name: 'ledProfileHeading',
  type: 'group-heading',
  label: 'Lamp Base LED Profile',
},
{
  name: 'basePx',
  type: 'number',
  label: 'Base LED Count',
  default: 36,
  optional: true,
  props: { min: 5, max: MAX_LEDS_BASE, placeholder: 'Number of LEDs' },
},
```

Between the heading and `basePx`, splice a conditional:

```ts
{
  name: 'ledProfileHeading',
  type: 'group-heading',
  label: 'Lamp Base LED Profile',
},
...(lampStore.state.lamp?.advancedEnabled
  ? [{
      name: 'baseBpp',
      type: 'select' as const,
      label: 'Base LED Type',
      default: 4,
      optional: true,
      options: [
        { value: 4, label: 'RGBWW' },
        { value: 3, label: 'RGB' },
      ],
    }]
  : []),
{
  name: 'basePx',
  ...
},
```

- [ ] **Step 3: Append a new shade-profile group at the end of the fields array (advanced only)**

At the very end of the computed fields array, after the `knockoutPixels` slot entry, add (also conditional):

```ts
...(lampStore.state.lamp?.advancedEnabled
  ? [
      {
        name: 'shadeProfileHeading',
        type: 'group-heading' as const,
        label: 'Lamp Shade LED Profile',
      },
      {
        name: 'shadeBpp',
        type: 'select' as const,
        label: 'Shade LED Type',
        default: 4,
        optional: true,
        options: [
          { value: 4, label: 'RGBWW' },
          { value: 3, label: 'RGB' },
        ],
      },
    ]
  : []),
```

This creates a new "Lamp Shade LED Profile" group with only the LED type select (no shade pixel count — that's a separate UI consideration).

- [ ] **Step 4: Update `formValues` to include the new field values**

Find the existing `formValues = computed({ get: () => ({...}), set: () => {} })` block. Inside the `get`, after `basePx: lampStore.state.base?.px ?? 36,`, add:

```ts
basePx: lampStore.state.base?.px ?? 36,
baseBpp: lampStore.state.base?.bpp ?? 4,
shadeBpp: lampStore.state.shade?.bpp ?? 4,
```

- [ ] **Step 5: Add handlers for the new fields in `handleFormUpdate`**

Find the existing `handleFormUpdate` function. It's a sequence of `if (values.X !== undefined && values.X !== formValues.value.X) lampStore.updateY(...)` checks. After the existing `basePx` handler, add:

```ts
if (values.baseBpp !== undefined && values.baseBpp !== formValues.value.baseBpp) {
  lampStore.updateBaseBpp(values.baseBpp as number)
}
if (values.shadeBpp !== undefined && values.shadeBpp !== formValues.value.shadeBpp) {
  lampStore.updateShadeBpp(values.shadeBpp as number)
}
```

- [ ] **Step 6: Type-check passes**

```bash
cd /Users/jerrett/projects/lamp-os/software/lamp-ui
npm run type-check
```

Expected: passes.

- [ ] **Step 7: Build the UI**

```bash
npm run build
```

Expected: succeeds.

- [ ] **Step 8: Build the rest and flash**

```bash
cd /Users/jerrett/projects/lamp-os
npm run build-and-flash
```

Expected: firmware + SPIFFS flash succeed.

- [ ] **Step 9: Manual E2E verification on hardware**

Walk through these in order — each gates the next:

1. Open the lamp UI (`http://192.168.4.1` or `lamp.local`). The Vue UI renders properly.
2. Navigate to Setup. The new "Base LED Type" / "Shade LED Type" fields are **absent** (advancedEnabled is still false by default).
3. Navigate to Info. Tap the logo 5 times within 3 seconds.
4. The page navigates to Setup (no banner, no animation, no message — just a tab switch).
5. In Setup, scroll to the LED Profile section. The new "Base LED Type" select is now visible above "Base LED Count".
6. Hard-refresh the page. The new field stays visible (because `advancedEnabled=true` is now in NVS and the fresh `/settings` GET returns it).
7. Change "Base LED Type" from RGBWW to RGB and save. The existing "save → reboot required" UI fires.
8. After reboot, light up the base strip with pure white (set base color to `#FFFFFF`). On an RGB-only strip, white should look right (no every-4th-LED-wrong artifact).
9. With `advancedEnabled=true`, go back to Info and tap the logo 5 more times. Nothing visible happens (the composable's early-return path runs, the navigation doesn't fire).

- [ ] **Step 10: Commit**

```bash
cd /Users/jerrett/projects/lamp-os
git add software/lamp-ui/src/pages/Setup.vue
git commit -m "setup: conditional LED Type fields gated by advancedEnabled"
```

---

## Final commit / merge

- [ ] **Step 1: Push to beta**

```bash
cd /Users/jerrett/projects/lamp-os
git push origin HEAD:refs/heads/beta
```

(Use `refs/heads/beta` explicitly because there's also a `beta` tag that resolves ambiguously.)

- [ ] **Step 2: Open a PR `beta → main` if you want this on main**

This is per-team workflow; the feature is feature-complete on beta as soon as task 8 lands and the manual verification passes.
