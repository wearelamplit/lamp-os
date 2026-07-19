<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'

import NavBar from './components/NavBar.vue'
import LamplitLogo from './components/LamplitLogo.vue'
import CritterNameplate from './components/CritterNameplate.vue'
import CritterIcon from './components/CritterIcon.vue'
import FormField from './components/FormField.vue'
import BrightnessSlider from './components/BrightnessSlider.vue'
import ColorGradient from './components/ColorGradient.vue'
import TextInput from './components/TextInput.vue'
import BooleanInput from './components/BooleanInput.vue'
import NumberInput from './components/NumberInput.vue'
import ExpressionsList from './components/expressions/ExpressionsList.vue'
import type { Config, ExpressionDescriptor, NearbyLamp } from './types'
import {
  SAFE_COLOR,
  paletteFromSegments,
  pxFromSegments,
  segmentsFromPalette,
} from './utils/configShape'
import { critterIndexFor } from './utils/critterIndex'
import { critterAssetPath, fetchCritterTemplate, recolorCritterSvg } from './utils/critterAsset'
import { hexwwToRgb } from './utils/colorUtils'

// Firmware does a full-replace on PUT: any field omitted from the body resets
// to its firmware default on the post-save reboot. So we GET the whole doc,
// mutate only the editable fields in place, and PUT the entire doc back.
const defaultConfig = (): Config => ({
  lamp: {
    name: '',
    brightness: 100,
    brightnessCeiling: 170,
    setup: true,
    advancedEnabled: false,
    webappEnabled: true,
    socialMode: 1,
    apBootMinutes: 2,
  },
  base: { ac: 0, segments: [{ px: 0, colors: [SAFE_COLOR] }], knockout: [] },
  shade: { segments: [{ px: 0, colors: [SAFE_COLOR] }] },
  expressions: [],
  homeMode: {
    ssid: '',
    brightness: 60,
    enabled: false,
    networkBound: false,
    socialDisabled: true,
    disabledExpressionTypes: ['glitchy'],
  },
})

// The UI edits one palette per surface plus base px/ac; these mirror the
// firmware's segments (single source on load, fanned back on save).
const baseColors = ref<string[]>([SAFE_COLOR])
const basePx = ref(0)
const shadeColors = ref<string[]>([SAFE_COLOR])

const tabs = [
  { id: 'home', label: 'Home' },
  { id: 'expressions', label: 'Expressions' },
  { id: 'social', label: 'Social' },
  { id: 'lamp-setup', label: 'Setup' },
  { id: 'info', label: 'Info' },
]

const brightnessCeilingOptions = [
  { label: 'Saver', value: 120 },
  { label: 'Standard', value: 170 },
  { label: 'Bright', value: 230 },
]

const socialModeOptions = [
  { label: 'Introvert', value: 0 },
  { label: 'Ambivert', value: 1 },
  { label: 'Extrovert', value: 2 },
]

const DISPOSITION_DEFAULT = 3
const dispositionOptions = [
  { value: 1, label: 'salty' },
  { value: 2, label: 'wary' },
  { value: 3, label: 'neutral' },
  { value: 4, label: 'fond' },
  { value: 5, label: 'smitten' },
]

const cfg = ref<Config>(defaultConfig())
const shadePx = computed(() => pxFromSegments(cfg.value.shade.segments))

// Advanced mode: 5 taps on the Info logo within ~2s reveals the LED-type
// pickers. Session-only, resets on reload.
const advanced = ref(false)
const byteOrderOptions = ['GRBW', 'GRB', 'BGR']
let logoTapCount = 0
let logoTapTimer: ReturnType<typeof setTimeout> | null = null
const tapLogo = () => {
  if (advanced.value) return
  logoTapCount += 1
  if (logoTapTimer) clearTimeout(logoTapTimer)
  logoTapTimer = setTimeout(() => (logoTapCount = 0), 2000)
  if (logoTapCount >= 5) {
    advanced.value = true
    logoTapCount = 0
  }
}
const baseByteOrder = computed(() => cfg.value.base.byteOrder ?? 'GRBW')
const shadeByteOrder = computed(() => cfg.value.shade.byteOrder ?? 'GRBW')
const expressionCatalog = ref<ExpressionDescriptor[]>([])

const nearbyLamps = ref<NearbyLamp[]>([])
const sortedNearby = computed(() =>
  [...nearbyLamps.value].sort((a, b) => b.lastSeenMs - a.lastSeenMs),
)
let nearbyTimer: ReturnType<typeof setInterval> | null = null

const fetchNearby = async () => {
  try {
    const res = await fetch('/api/nearby')
    if (!res.ok) return
    const data = await res.json()
    if (Array.isArray(data)) nearbyLamps.value = data as NearbyLamp[]
  } catch {
    // Keep the last list on a failed poll.
  }
}

const stopNearbyPolling = () => {
  if (nearbyTimer) clearInterval(nearbyTimer)
  nearbyTimer = null
}

// PUT /api/dispositions is a full-map replace, so the local map is the source
// of truth: GET it on entering Social, mutate one key on a pick, PUT it whole.
const dispositions = ref<Record<string, number>>({})
const dispositionsLoaded = ref(false)
const dispModalLamp = ref<NearbyLamp | null>(null)
const dispError = ref('')

const dispositionLabel = (lampId?: string) =>
  lampId && dispositions.value[lampId]
    ? dispositionOptions.find((o) => o.value === dispositions.value[lampId])?.label
    : ''

const currentDisposition = computed(() => {
  const id = dispModalLamp.value?.lampId
  return id ? (dispositions.value[id] ?? DISPOSITION_DEFAULT) : DISPOSITION_DEFAULT
})

const fetchDispositions = async () => {
  dispositionsLoaded.value = false
  try {
    const res = await fetch('/api/dispositions')
    if (!res.ok) return
    const data = await res.json()
    if (data && typeof data === 'object') {
      dispositions.value = data as Record<string, number>
      dispositionsLoaded.value = true
    }
  } catch {
    // Keep the last map on a failed fetch.
  }
}

const openDisposition = (lamp: NearbyLamp) => {
  if (!lamp.lampId) return
  dispError.value = ''
  dispModalLamp.value = lamp
}

const closeDisposition = () => {
  dispModalLamp.value = null
}

const onDispKeydown = (e: KeyboardEvent) => {
  if (e.key === 'Escape') closeDisposition()
}

watch(dispModalLamp, (lamp) => {
  if (lamp) window.addEventListener('keydown', onDispKeydown)
  else window.removeEventListener('keydown', onDispKeydown)
})

const pickDisposition = async (value: number) => {
  const id = dispModalLamp.value?.lampId
  if (!id) return
  if (!dispositionsLoaded.value) {
    dispError.value = 'Reconnecting — try again'
    fetchDispositions()
    return
  }
  const prev = dispositions.value[id]
  dispositions.value = { ...dispositions.value, [id]: value }
  let ok = false
  try {
    // ponytail: whole-map replace capped at 512 bytes (~23 peers) shared with the BLE path; single-key PATCH if peer count grows.
    const res = await fetch('/api/dispositions', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(dispositions.value),
    })
    ok = res.ok
  } catch {
    ok = false
  }
  if (ok) {
    closeDisposition()
    return
  }
  const reverted = { ...dispositions.value }
  if (prev === undefined) delete reverted[id]
  else reverted[id] = prev
  dispositions.value = reverted
  dispError.value = 'Save failed — try again'
}

// Firmware stores the muted state; the toggle reads as "social ON".
const socialEnabled = computed({
  get: () => !cfg.value.homeMode.socialDisabled,
  set: (v: boolean) => (cfg.value.homeMode.socialDisabled = !v),
})

const isExpressionEnabled = (id: string) =>
  !cfg.value.homeMode.disabledExpressionTypes.includes(id)

const setExpressionEnabled = (id: string, enabled: boolean) => {
  const disabled = cfg.value.homeMode.disabledExpressionTypes
  if (enabled) {
    cfg.value.homeMode.disabledExpressionTypes = disabled.filter((t) => t !== id)
  } else if (!disabled.includes(id)) {
    disabled.push(id)
  }
}

const ready = ref(false)
const saving = ref(false)
// Set only after a real GET succeeds; Save is a full-replace PUT, so saving
// before this is true would blast defaultConfig() onto the lamp.
const loadedRealConfig = ref(false)
const activeTab = ref('home')
const errorMessage = ref('')

// Firmware-owned: false = the variant fixes its colors, hide the picker.
const baseColorsEditable = computed(() => cfg.value.base.colorsEditable !== false)
const shadeColorsEditable = computed(() => cfg.value.shade.colorsEditable !== false)

const nameValid = computed(() => {
  const n = cfg.value.lamp.name.trim()
  return n.length >= 3 && n.length <= 12
})
const passwordValid = computed(() => {
  const p = password.value
  return p === '' || (p.length >= 8 && p.length <= 16)
})
// Build-time version, injected by the lamp-ui build (VITE_FW_VERSION). Shown
// in the info tab; doubles as a visible signal of which build's UI is live.
const fwVersion = import.meta.env.VITE_FW_VERSION || 'dev'
const wsConnected = ref(false)

// Password is write-only: GET never returns it, so this starts empty. Only a
// non-empty value is injected into the PUT body; empty keeps the existing one.
// A typed value replaces the password (and is length-gated); empty keeps it.
const password = ref('')
// The current password revealed by /api/unlock, shown read-only so the user
// can see it without the value counting as an edit or hitting the length gate.
const revealedPassword = ref('')

// A password-protected lamp gates the editor until /api/unlock confirms.
const locked = ref(false)
const unlockPassword = ref('')
const unlockError = ref('')

const unlock = async () => {
  unlockError.value = ''
  try {
    const res = await fetch('/api/unlock', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ password: unlockPassword.value }),
    })
    if (res.ok) {
      const data = await res.json()
      revealedPassword.value = data.password ?? ''
      locked.value = false
      unlockPassword.value = ''
    } else {
      unlockError.value = 'Incorrect password'
    }
  } catch {
    unlockError.value = 'Incorrect password'
  }
}

// Baseline for change detection; password + editable palettes live outside cfg
// so track them too.
const originalSettings = ref('')
const editSnapshot = () =>
  JSON.stringify([cfg.value, baseColors.value, basePx.value, shadeColors.value])
const hasChanges = computed(
  () => editSnapshot() !== originalSettings.value || password.value !== '',
)

const disabled = computed(() => !wsConnected.value)

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

const sendPreview = (payload: Record<string, string[] | number | string | boolean>) => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload))
  }
}

const testExpression = (type: string, target: number) => {
  sendPreview({ a: 'test_expression', type, target })
}

// surface 1=base, 2=shade. Tells the lamp to pause expressions on that
// surface while its color editor is open so the picked color reads clean.
const sendEditSession = (surface: number, open: boolean) => {
  sendPreview({ a: 'edit_session', surface, open })
}

// Re-assert the saved base/shade colors on the lamp to undo an
// expression-color preview when leaving the expressions tab.
const restoreColorPreview = () => {
  if (!ready.value) return
  sendPreview({
    a: 'test_expression_complete',
    baseColors: [...baseColors.value],
    shadeColors: [...shadeColors.value],
  })
}

// Light the lamp with an expression's palette while its colors are edited,
// routed to the surface the expression targets (1=Shade, 2=Base, 3=Both).
// restoreColorPreview() puts the real colors back.
const previewExpressionColors = (colors: string[], target: number) => {
  if (!ready.value || !colors.length) return
  if (target === 1 || target === 3) sendPreview({ shadeColors: [...colors] })
  if (target === 2 || target === 3) sendPreview({ baseColors: [...colors] })
}

const connectWs = () => {
  ws = new WebSocket(`ws://${location.host}/ws`)
  ws.onopen = () => {
    wsConnected.value = true
  }
  ws.onclose = () => {
    wsConnected.value = false
    reconnectTimer = setTimeout(connectWs, 2000)
  }
}

// Live preview is FLAT and only covers base/shade colors + brightness.
// Knockout / expressions / home mode apply on save+reboot — no preview.
watch(
  baseColors,
  (value) => ready.value && sendPreview({ baseColors: [...value] }),
  { deep: true },
)

watch(
  shadeColors,
  (value) => ready.value && sendPreview({ shadeColors: [...value] }),
  { deep: true },
)

watch(
  () => cfg.value.lamp.brightness,
  (value) => ready.value && sendPreview({ brightness: value }),
)

// Leaving the expressions tab undoes any lingering expression-color preview;
// the Social tab polls /api/nearby only while it's open.
watch(activeTab, async (tab, prev) => {
  if (prev === 'expressions') restoreColorPreview()
  if (tab === 'social') {
    fetchNearby()
    await fetchDispositions()
    nearbyTimer = setInterval(fetchNearby, 10000)
  } else {
    dispositionsLoaded.value = false
    stopNearbyPolling()
  }
})

// Keep the dense per-pixel knockout array sized to the base pixel count.
watch(basePx, (px) => {
  if (!ready.value) return
  const k = cfg.value.base.knockout
  if (px > k.length) while (k.length < px) k.push(100)
  else if (px < k.length) k.length = px
})

const save = async () => {
  if (!loadedRealConfig.value) return
  if (!hasChanges.value || saving.value) return
  if (!nameValid.value || !passwordValid.value) return
  saving.value = true
  errorMessage.value = ''
  if (password.value) cfg.value.lamp.password = password.value
  // Saving through the web UI claims the lamp (stray -> adopted).
  cfg.value.lamp.setup = true
  // Fan the edited palettes back into the segments the firmware parses (it
  // prefers `segments` over any flat `colors`); px only touches base.
  cfg.value.base.segments = segmentsFromPalette(
    cfg.value.base.segments,
    baseColors.value,
    basePx.value,
  )
  cfg.value.shade.segments = segmentsFromPalette(cfg.value.shade.segments, shadeColors.value)
  try {
    const res = await fetch('/api/settings', {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg.value),
    })
    if (res.ok) {
      // Don't keep the password in the held doc once it's been sent.
      delete cfg.value.lamp.password
      password.value = ''
      originalSettings.value = editSnapshot()
    } else {
      errorMessage.value = 'Save failed — try again'
    }
  } catch {
    // leave hasChanges true so the user can retry
    errorMessage.value = 'Save failed — try again'
  } finally {
    saving.value = false
  }
}

// Sets tab title + favicon once when settings load; doesn't track later
// color-slider edits.
const setTabChrome = async () => {
  document.title = cfg.value.lamp.name || 'Lamp config'
  if (!cfg.value.lamp.lampId) return
  try {
    const template = await fetchCritterTemplate(critterAssetPath(critterIndexFor(cfg.value.lamp.lampId)))
    const svg = recolorCritterSvg(
      template,
      hexwwToRgb(shadeColors.value[0] ?? SAFE_COLOR),
      hexwwToRgb(baseColors.value[cfg.value.base.ac ?? 0] ?? SAFE_COLOR),
    )
    let link = document.querySelector<HTMLLinkElement>('link[rel="icon"]')
    if (!link) {
      link = document.createElement('link')
      link.rel = 'icon'
      document.head.appendChild(link)
    }
    link.type = 'image/svg+xml'
    link.href = `data:image/svg+xml,${encodeURIComponent(svg)}`
  } catch {
    // Keep the default favicon.
  }
}

onMounted(async () => {
  try {
    const res = await fetch('/api/settings')
    if (!res.ok) throw new Error('settings request failed')
    const data = await res.json()
    if (!data || !data.lamp) throw new Error('malformed settings response')
    cfg.value = data as Config
    // Derive the editable palettes from the firmware's segments.
    baseColors.value = paletteFromSegments(cfg.value.base.segments)
    basePx.value = pxFromSegments(cfg.value.base.segments)
    shadeColors.value = paletteFromSegments(cfg.value.shade.segments)
    if (!Array.isArray(cfg.value.base.knockout)) cfg.value.base.knockout = []
    if (!Array.isArray(cfg.value.expressions)) cfg.value.expressions = []
    if (!Array.isArray(cfg.value.homeMode.disabledExpressionTypes))
      cfg.value.homeMode.disabledExpressionTypes = []
    locked.value = cfg.value.lamp.hasPassword === true
    delete cfg.value.lamp.hasPassword
    loadedRealConfig.value = true
  } catch {
    // Leave defaults on screen but keep Save gated; see loadedRealConfig.
    errorMessage.value = "Couldn't load settings from the lamp"
  }
  try {
    const res = await fetch('/api/expressions')
    if (!res.ok) throw new Error('expressions request failed')
    const data = await res.json()
    if (data && Array.isArray(data.expressions)) {
      expressionCatalog.value = data.expressions as ExpressionDescriptor[]
    }
  } catch {
    // Old firmware without the catalog endpoint; expressions tab degrades.
  }
  originalSettings.value = editSnapshot()
  ready.value = true
  connectWs()
  setTabChrome()
})

onUnmounted(() => {
  if (ws) {
    ws.onclose = null
    ws.close()
  }
  if (reconnectTimer) clearTimeout(reconnectTimer)
  stopNearbyPolling()
  window.removeEventListener('keydown', onDispKeydown)
})
</script>

<template>
  <div class="home">
    <div v-if="errorMessage" class="error-banner" role="alert">
      <span>{{ errorMessage }}</span>
      <button type="button" class="error-dismiss" aria-label="Dismiss" @click="errorMessage = ''">×</button>
    </div>

    <div v-if="ready && locked" class="container">
      <main>
        <div class="unlock-gate">
          <h1 class="gold">Locked</h1>
          <p>This lamp is password protected. Enter its password to make changes.</p>
          <FormField id="unlock-password">
            <TextInput
              v-model="unlockPassword"
              type="password"
              placeholder="Password"
              :max-length="16"
              @keyup.enter="unlock"
            />
          </FormField>
          <div v-if="unlockError" class="unlock-error">{{ unlockError }}</div>
          <button type="button" class="unlock-button" @click="unlock">Unlock</button>
        </div>
      </main>
    </div>

    <div v-if="ready && !locked" class="container">
      <main>
        <NavBar :tabs="tabs" :active-tab="activeTab" @update:active-tab="activeTab = $event" />

        <div class="tab-content">
          <!-- Home -->
          <section v-if="activeTab === 'home'" class="tab-panel" aria-label="Home settings">
            <CritterNameplate
              :name="cfg.lamp.name"
              :lamp-id="cfg.lamp.lampId"
              :base-color="baseColors[cfg.base.ac ?? 0]"
              :shade-color="shadeColors[0]"
              :size="96"
            />

            <h1 class="gold">
              Lamp Brightness
              <span v-if="hasChanges" class="preview-badge">Preview, not saved</span>
            </h1>
            <FormField id="brightness">
              <BrightnessSlider
                v-model="cfg.lamp.brightness"
                id="brightness"
                :min="0"
                :max="100"
                append="%"
                :disabled="disabled || cfg.homeMode.enabled"
              />
            </FormField>

            <h1 class="yellow">
              Lamp Color Settings
              <span v-if="hasChanges" class="preview-badge">Preview, not saved</span>
            </h1>
            <FormField label="Shade" id="shadeColors">
              <ColorGradient
                v-if="shadeColorsEditable"
                v-model="shadeColors"
                :show-add-button="false"
                :max-colors="1"
                :disabled="disabled"
                @edit-session="(open) => sendEditSession(2, open)"
              />
              <div v-else class="info-text">Shade color is set by firmware.</div>
            </FormField>

            <FormField label="Base" id="baseColors">
              <ColorGradient
                v-if="baseColorsEditable"
                v-model="baseColors"
                :max-colors="5"
                :disabled="disabled"
                :active-color="cfg.base.ac ?? 0"
                @update:active-color="(v) => (cfg.base.ac = v)"
                @edit-session="(open) => sendEditSession(1, open)"
              />
              <div v-else class="info-text">Base color is set by firmware.</div>
            </FormField>
          </section>

          <!-- Expressions -->
          <section v-if="activeTab === 'expressions'" class="tab-panel" aria-label="Expression settings">
            <div class="expressions-instructions">
              <p>
                Add expressions to give your lamp personality. Expressions are behaviors that
                trigger randomly to create visual effects.
              </p>
            </div>
            <ExpressionsList
              v-model="cfg.expressions"
              :catalog="expressionCatalog"
              :base-px="basePx"
              :shade-px="shadePx"
              :disabled="disabled"
              @preview="previewExpressionColors"
              @preview-end="restoreColorPreview"
              @test-expression="testExpression"
            />
          </section>

          <!-- Social -->
          <section v-if="activeTab === 'social'" class="tab-panel" aria-label="Social settings">
            <h1 class="gold">Personality</h1>
            <FormField label="Sociability" id="socialMode">
              <div class="targets">
                <button
                  v-for="opt in socialModeOptions"
                  :key="opt.value"
                  type="button"
                  class="target"
                  :class="{ active: cfg.lamp.socialMode === opt.value }"
                  :disabled="disabled"
                  @click="cfg.lamp.socialMode = opt.value"
                >
                  {{ opt.label }}
                </button>
              </div>
              <div class="info-text">
                How sociable your lamp is: an introvert keeps to itself, an extrovert greets and
                reacts to nearby lamps often.
              </div>
            </FormField>

            <h1 class="lime">Recently Seen</h1>
            <div v-if="sortedNearby.length" class="nearby-list">
              <div
                v-for="lamp in sortedNearby"
                :key="lamp.lampId ?? lamp.name + lamp.lastSeenMs"
                class="nearby-row"
                :class="{ tappable: lamp.lampId }"
                :role="lamp.lampId ? 'button' : undefined"
                :tabindex="lamp.lampId ? 0 : undefined"
                @click="openDisposition(lamp)"
                @keyup.enter="openDisposition(lamp)"
              >
                <div class="nearby-critter">
                  <CritterIcon
                    v-if="lamp.lampId"
                    :lamp-id="lamp.lampId"
                    :shade="lamp.shade"
                    :base="lamp.base"
                  />
                </div>
                <div class="nearby-name">
                  {{ lamp.name }}
                  <span v-if="dispositionLabel(lamp.lampId)" class="nearby-disposition">
                    {{ dispositionLabel(lamp.lampId) }}
                  </span>
                </div>
                <div class="nearby-swatches">
                  <span
                    class="nearby-swatch"
                    :style="{ background: hexwwToRgb(lamp.shade) }"
                    title="Shade"
                  ></span>
                  <span
                    class="nearby-swatch"
                    :style="{ background: hexwwToRgb(lamp.base) }"
                    title="Base"
                  ></span>
                </div>
                <span v-if="lamp.lampId" class="nearby-chevron" aria-hidden="true">›</span>
              </div>
            </div>
            <div v-else class="info-text">No lamps nearby yet.</div>
          </section>

          <!-- Lamp Setup -->
          <section v-if="activeTab === 'lamp-setup'" class="tab-panel" aria-label="Setup settings">
            <h1 class="gold">Lamp Name</h1>
            <FormField id="name">
              <TextInput
                v-model="cfg.lamp.name"
                placeholder="Enter a name for your lamp"
                :disabled="disabled"
                :max-length="12"
                pattern="[a-z]+"
                transform="lowercase"
              />
              <div class="info-text">
                Names must be all lowercase letters and between 3-12 characters.
              </div>
              <div v-if="!nameValid" class="field-error">
                Name must be between 3 and 12 characters.
              </div>
            </FormField>

            <h1 class="yellow">Lamp Password</h1>
            <FormField v-if="revealedPassword" label="Current Password" id="currentPassword">
              <input class="revealed-password" :value="revealedPassword" readonly />
            </FormField>
            <FormField id="password">
              <TextInput
                v-model="password"
                placeholder="Leave unchanged"
                :disabled="disabled"
                :max-length="16"
                pattern="[ -~]+"
              />
              <div class="info-text">
                Optional password to protect your lamp from changes. Between 8-16 characters. Leave
                empty to keep the current password.
              </div>
              <div v-if="!passwordValid" class="field-error">
                Password must be between 8 and 16 characters.
              </div>
            </FormField>

            <h1 class="gold">Setup Access</h1>
            <FormField label="Keep Setup Available" id="apBootMinutes">
              <select
                v-model.number="cfg.lamp.apBootMinutes"
                class="ap-boot-select"
                :disabled="disabled"
              >
                <option :value="2">2 minutes</option>
                <option :value="5">5 minutes</option>
                <option :value="10">10 minutes</option>
                <option :value="0">Always on</option>
              </select>
              <div class="info-text">
                How long the setup WiFi network stays available after the lamp boots.
              </div>
            </FormField>

            <h1 class="yellow">Power</h1>
            <FormField label="Battery Saver" id="brightnessCeiling">
              <div class="targets">
                <button
                  v-for="opt in brightnessCeilingOptions"
                  :key="opt.value"
                  type="button"
                  class="target"
                  :class="{ active: cfg.lamp.brightnessCeiling === opt.value }"
                  :disabled="disabled"
                  @click="cfg.lamp.brightnessCeiling = opt.value"
                >
                  {{ opt.label }}
                </button>
              </div>
              <div class="info-text">
                Caps the lamp's maximum brightness. Lower saves power; higher runs brighter.
              </div>
            </FormField>

            <h1 class="lime">At-Home Mode</h1>
            <div class="mode-toggles">
              <FormField label="Home Mode" id="homeMode">
                <BooleanInput v-model="cfg.homeMode.enabled" :disabled="disabled" />
              </FormField>

              <div v-if="cfg.homeMode.enabled" class="home-mode-settings">
                <FormField label="Home Mode Brightness" id="homeModeBrightness">
                  <BrightnessSlider
                    v-model="cfg.homeMode.brightness"
                    id="homeModeBrightness"
                    :min="0"
                    :max="100"
                    append="%"
                    :disabled="disabled"
                  />
                </FormField>

                <FormField label="Only on my home network" id="homeModeNetworkBound">
                  <BooleanInput v-model="cfg.homeMode.networkBound" :disabled="disabled" />
                  <div class="info-text">
                    Home mode activates only when your WiFi network is in range.
                  </div>
                </FormField>

                <FormField
                  v-if="cfg.homeMode.networkBound"
                  label="Home Network SSID"
                  id="homeModeSSID"
                >
                  <TextInput
                    v-model="cfg.homeMode.ssid"
                    placeholder="Enter your home WiFi name"
                    :disabled="disabled"
                    :max-length="32"
                    pattern="[ -~]+"
                  />
                  <div class="info-text">
                    When the lamp detects this WiFi network, it activates home-only behaviors.
                  </div>
                </FormField>

                <FormField label="Social behaviours" id="homeModeSocial">
                  <BooleanInput v-model="socialEnabled" :disabled="disabled" />
                  <div class="info-text">Greetings and nearby-lamp reactions.</div>
                </FormField>

                <FormField
                  v-if="expressionCatalog.length"
                  label="Expressions"
                  id="homeModeExpressions"
                >
                  <div class="home-mode-expressions">
                    <div v-for="d in expressionCatalog" :key="d.id" class="expression-toggle-row">
                      <span class="expression-toggle-label">{{ d.name }}</span>
                      <BooleanInput
                        :model-value="isExpressionEnabled(d.id)"
                        :disabled="disabled"
                        @update:model-value="(v) => setExpressionEnabled(d.id, v)"
                      />
                    </div>
                  </div>
                </FormField>
              </div>
            </div>

            <h1 class="green">Lamp Base LED Profile</h1>
            <FormField label="Base LED Count" id="baseLeds">
              <NumberInput
                v-model="basePx"
                :min="5"
                :max="50"
                placeholder="Number of LEDs"
                :disabled="disabled"
              />
            </FormField>

            <FormField label="Per-Pixel Brightness Adjustment" id="baseKnockoutPixels" expandable>
              <div class="pixel-grid">
                <div v-for="(_, i) in cfg.base.knockout" :key="i" class="pixel-row">
                  <label class="pixel-label">LED {{ i + 1 }}</label>
                  <BrightnessSlider
                    :model-value="cfg.base.knockout[i]"
                    :id="`knockout-pixel-${i}`"
                    :min="0"
                    :max="100"
                    append="%"
                    :disabled="disabled"
                    @update:model-value="(v) => (cfg.base.knockout[i] = v)"
                  />
                </div>
              </div>
            </FormField>

            <template v-if="advanced">
              <h1 class="gold">Advanced</h1>
              <FormField label="Base LED Type" id="baseByteOrder">
                <div class="targets">
                  <button
                    v-for="opt in byteOrderOptions"
                    :key="opt"
                    type="button"
                    class="target"
                    :class="{ active: baseByteOrder === opt }"
                    :disabled="disabled"
                    @click="cfg.base.byteOrder = opt"
                  >
                    {{ opt }}
                  </button>
                </div>
              </FormField>

              <FormField label="Shade LED Type" id="shadeByteOrder">
                <div class="targets">
                  <button
                    v-for="opt in byteOrderOptions"
                    :key="opt"
                    type="button"
                    class="target"
                    :class="{ active: shadeByteOrder === opt }"
                    :disabled="disabled"
                    @click="cfg.shade.byteOrder = opt"
                  >
                    {{ opt }}
                  </button>
                </div>
                <div class="info-text">
                  LED channel order. Change only if your lamp's colors are wrong.
                </div>
              </FormField>
            </template>
          </section>

          <!-- Info -->
          <section v-if="activeTab === 'info'" class="tab-panel" aria-label="Information">
            <div class="info-content">
              <div class="logo-container" @click="tapLogo">
                <LamplitLogo />
              </div>
              <p v-if="advanced" class="advanced-note">Advanced unlocked</p>
              <p>
                Lamplit Art Society is a non-profit collective sparking connection and creativity
                through shared lamp art. More at
                <a href="https://lamplit.ca" target="_blank" rel="noopener">lamplit.ca</a>
              </p>
              <p class="version">Firmware {{ fwVersion }}</p>
            </div>
          </section>
        </div>
      </main>
    </div>

    <div v-if="dispModalLamp" class="disp-overlay" @click.self="closeDisposition">
      <div class="disp-modal" role="dialog" aria-modal="true" aria-label="Set disposition">
        <div class="disp-header">
          <div class="disp-critter">
            <CritterIcon
              v-if="dispModalLamp.lampId"
              :lamp-id="dispModalLamp.lampId"
              :shade="dispModalLamp.shade"
              :base="dispModalLamp.base"
            />
          </div>
          <div class="disp-name">{{ dispModalLamp.name }}</div>
          <button type="button" class="disp-close" aria-label="Close" @click="closeDisposition">×</button>
        </div>
        <p class="disp-prompt">How does your lamp feel about this one?</p>
        <div class="disp-options">
          <button
            v-for="opt in dispositionOptions"
            :key="opt.value"
            type="button"
            class="disp-option"
            :class="{ active: currentDisposition === opt.value }"
            @click="pickDisposition(opt.value)"
          >
            {{ opt.label }}
          </button>
        </div>
        <div v-if="dispError" class="disp-error">{{ dispError }}</div>
      </div>
    </div>

    <!-- Floating Save Button -->
    <div v-if="ready && !locked" class="floating-save-container">
      <button
        class="floating-save-button"
        :class="{
          'has-changes': hasChanges && !disabled && nameValid && passwordValid && loadedRealConfig,
          saving: saving,
          'no-changes': !hasChanges || disabled || !nameValid || !passwordValid || !loadedRealConfig,
        }"
        @click="save"
        :disabled="!hasChanges || saving || disabled || !nameValid || !passwordValid || !loadedRealConfig"
      >
        <span v-if="!loadedRealConfig">Unavailable</span>
        <span v-else-if="disabled">Connecting...</span>
        <span v-else-if="saving">Saving...</span>
        <span v-else-if="!nameValid || !passwordValid">Fix errors</span>
        <span v-else-if="hasChanges">Save Changes</span>
        <span v-else>No Changes</span>
      </button>
    </div>
  </div>
</template>

<style>
#app {
  width: 100%;
}

button,
input,
select,
textarea,
a,
[role='button'],
[tabindex] {
  touch-action: manipulation;
  -webkit-touch-callout: none;
  -webkit-user-select: none;
  -moz-user-select: none;
  user-select: none;
}

input[type='text'],
input[type='number'],
input[type='password'],
textarea {
  -webkit-user-select: text;
  -moz-user-select: text;
  user-select: text;
}
</style>

<style scoped>
.home {
  --nav-h: 58px;
  height: 100vh;
  height: 100dvh;
  display: flex;
  flex-direction: column;
  background: var(--brand-midnight-black);
  width: 100%;
}

.container {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  -webkit-overflow-scrolling: touch;
  width: 100%;
}

.tab-content {
  min-height: 200px;
  padding: 16px 16px calc(var(--nav-h) + 80px + env(safe-area-inset-bottom));
}

.tab-panel {
  animation: fadeIn 0.3s ease-in-out;
}

@keyframes fadeIn {
  from {
    opacity: 0;
    transform: translateY(10px);
  }
  to {
    opacity: 1;
    transform: translateY(0);
  }
}

h1 {
  font-size: 1.1rem;
  font-weight: 700;
  margin: 24px 0 12px;
}

.preview-badge {
  margin-left: 8px;
  padding: 2px 8px;
  border-radius: 10px;
  background: rgba(68, 108, 156, 0.15);
  color: var(--brand-aurora-blue);
  font-size: 0.65rem;
  font-weight: 500;
  vertical-align: middle;
}

.info-content {
  padding: 20px;
  color: var(--brand-slate-grey);
}

.info-content a {
  color: var(--brand-aurora-blue);
  text-decoration: underline;
  font-weight: 600;
}

.logo-container {
  display: flex;
  justify-content: center;
  margin-bottom: 24px;
}

.info-content :deep(.logo) {
  width: 120px;
  max-width: 40%;
  height: auto;
}

.advanced-note {
  text-align: center;
  font-size: 0.75rem;
  color: var(--brand-lumen-green);
}

.targets {
  display: flex;
  gap: 8px;
}

.target {
  flex: 1;
  padding: 8px 12px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 4px;
  cursor: pointer;
}

.target.active {
  background: rgba(64, 176, 0, 0.2);
  color: var(--brand-lumen-green);
  border-color: var(--brand-lumen-green);
}

.target:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.info-content p {
  line-height: 1.6;
  margin-bottom: 16px;
}

.mode-toggles {
  display: flex;
  flex-direction: column;
  gap: 0;
}

.mode-toggles > .form-field {
  width: 100%;
}

.home-mode-settings {
  animation: fadeIn 0.3s ease-in-out;
}

.home-mode-settings .form-field {
  margin-top: 8px;
  margin-bottom: 32px;
}

.home-mode-expressions {
  display: flex;
  flex-direction: column;
  gap: 4px;
}

.expression-toggle-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 10px 8px;
  background: rgba(253, 253, 253, 0.02);
  border-radius: 6px;
}

.expression-toggle-label {
  font-size: 0.9rem;
  color: var(--brand-fog-grey);
  font-weight: 500;
}

.expression-toggle-row :deep(.boolean-input-container) {
  width: auto;
}

.info-text {
  margin-top: 12px;
  padding: 8px 12px;
  background: rgba(68, 108, 156, 0.08);
  border-left: 2px solid var(--brand-aurora-blue);
  border-radius: 4px;
  font-size: 0.75rem;
  line-height: 1.4;
  color: var(--brand-slate-grey);
}

.revealed-password {
  width: 100%;
  height: 52px;
  padding: 14px 16px;
  border: 2px solid var(--color-background-mute);
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 500;
  background-color: var(--color-background-mute);
  color: var(--color-text);
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
  opacity: 0.8;
}

.field-error {
  margin-top: 8px;
  font-size: 0.8rem;
  font-weight: 500;
  color: var(--color-error);
}

.error-banner {
  position: fixed;
  top: 12px;
  left: 12px;
  right: 12px;
  z-index: 1002;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  max-width: 450px;
  margin: 0 auto;
  padding: 12px 16px;
  border-radius: 8px;
  background: rgba(248, 113, 113, 0.15);
  border: 1px solid var(--color-error);
  color: var(--color-error);
  font-size: 0.85rem;
  font-weight: 500;
  backdrop-filter: blur(10px);
}

.error-dismiss {
  background: none;
  border: none;
  color: var(--color-error);
  font-size: 1.3rem;
  line-height: 1;
  cursor: pointer;
  padding: 0 4px;
}

.ap-boot-select {
  width: 100%;
  height: 52px;
  padding: 14px 16px;
  border: 2px solid var(--color-background-mute);
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 500;
  background-color: var(--color-background);
  color: var(--color-text);
  box-shadow: 0 2px 4px rgba(0, 0, 0, 0.2);
  cursor: pointer;
}

.ap-boot-select:focus {
  outline: none;
  border-color: var(--brand-aurora-blue);
  box-shadow: 0 0 0 3px rgba(68, 108, 156, 0.2);
}

.ap-boot-select:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.unlock-gate {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 48px 20px;
}

.unlock-gate p {
  color: var(--brand-slate-grey);
  line-height: 1.5;
  margin: 0;
}

.unlock-error {
  color: var(--color-error);
  font-size: 0.85rem;
  font-weight: 500;
}

.unlock-button {
  padding: 14px;
  border: none;
  border-radius: 12px;
  font-size: 1rem;
  font-weight: 600;
  cursor: pointer;
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
}

.expressions-instructions {
  margin-bottom: 24px;
  padding: 12px 16px;
  background: rgba(68, 108, 156, 0.06);
  border-radius: 6px;
}

.expressions-instructions p {
  margin: 0;
  font-size: 0.85rem;
  line-height: 1.5;
  color: var(--brand-fog-grey);
}

.nearby-list {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.nearby-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  background: rgba(253, 253, 253, 0.02);
  border-radius: 8px;
}

.nearby-critter {
  width: 32px;
  flex-shrink: 0;
}

.nearby-name {
  flex: 1;
  font-size: 0.95rem;
  font-weight: 600;
  color: var(--brand-fog-grey);
}

.nearby-swatches {
  display: flex;
  gap: 6px;
  flex-shrink: 0;
}

.nearby-swatch {
  width: 20px;
  height: 20px;
  border-radius: 5px;
  border: 1px solid rgba(255, 255, 255, 0.15);
}

.nearby-row.tappable {
  cursor: pointer;
  transition: background 0.15s ease;
}

.nearby-row.tappable:hover {
  background: rgba(253, 253, 253, 0.06);
}

.nearby-disposition {
  margin-left: 8px;
  font-size: 0.7rem;
  font-weight: 500;
  color: var(--brand-glow-pink);
}

.nearby-chevron {
  flex-shrink: 0;
  font-size: 1.3rem;
  line-height: 1;
  color: var(--brand-slate-grey);
}

.disp-overlay {
  position: fixed;
  inset: 0;
  z-index: 1100;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 20px;
  background: rgba(0, 0, 0, 0.6);
  backdrop-filter: blur(4px);
}

.disp-modal {
  width: 100%;
  max-width: 360px;
  padding: 20px;
  border-radius: 16px;
  background: var(--color-background-soft);
  border: 1px solid var(--color-border);
  box-shadow: 0 12px 48px rgba(0, 0, 0, 0.5);
  animation: fadeIn 0.2s ease-in-out;
}

.disp-header {
  display: flex;
  align-items: center;
  gap: 12px;
}

.disp-critter {
  width: 36px;
  flex-shrink: 0;
}

.disp-name {
  flex: 1;
  font-size: 1.05rem;
  font-weight: 700;
  color: var(--brand-lamp-white);
}

.disp-close {
  background: none;
  border: none;
  color: var(--brand-slate-grey);
  font-size: 1.5rem;
  line-height: 1;
  cursor: pointer;
  padding: 0 4px;
}

.disp-prompt {
  margin: 16px 0 12px;
  font-size: 0.85rem;
  color: var(--brand-slate-grey);
}

.disp-options {
  display: flex;
  flex-direction: column;
  gap: 8px;
}

.disp-option {
  padding: 12px 16px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--brand-light-grey);
  border: 1px solid rgba(255, 255, 255, 0.2);
  border-radius: 8px;
  font-family: inherit;
  font-size: 0.95rem;
  font-weight: 500;
  text-transform: capitalize;
  cursor: pointer;
  transition: all 0.15s ease;
}

.disp-option:hover {
  background: rgba(255, 255, 255, 0.08);
}

.disp-option.active {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  border-color: transparent;
}

.disp-error {
  margin-top: 12px;
  font-size: 0.8rem;
  font-weight: 500;
  color: var(--color-error);
}

.pixel-grid {
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-height: 400px;
  overflow-y: auto;
  padding: 8px;
  background: rgba(253, 253, 253, 0.02);
  border-radius: 8px;
  border: 1px solid var(--color-border);
}

.pixel-row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 8px;
  background: rgba(253, 253, 253, 0.02);
}

.pixel-label {
  min-width: 80px;
  font-size: 0.9rem;
  color: var(--brand-fog-grey);
  font-weight: 500;
}

.pixel-row :deep(.number-slider) {
  flex: 1;
}

/* Floating Save Button */
.floating-save-container {
  position: fixed;
  bottom: calc(var(--nav-h) + 14px + env(safe-area-inset-bottom));
  z-index: 1000;
  pointer-events: none;
  display: flex;
  justify-content: center;
  width: 100%;
}

.floating-save-button {
  pointer-events: auto;
  padding: 16px 32px;
  border: none;
  border-radius: 50px;
  font-size: 1rem;
  font-weight: 600;
  cursor: pointer;
  transition: all 0.3s ease;
  box-shadow:
    0 20px 60px rgba(0, 0, 0, 0.4),
    0 8px 32px rgba(0, 0, 0, 0.3),
    0 0 0 1px rgba(255, 255, 255, 0.1);
  backdrop-filter: blur(10px);
  font-family: inherit;
  min-width: 160px;
  text-align: center;
}

.floating-save-button.no-changes {
  background: var(--color-background-mute) !important;
  color: var(--brand-slate-grey);
  cursor: not-allowed;
}

.floating-save-button.has-changes {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-glow-pink));
  color: var(--brand-lamp-white);
  cursor: pointer;
}

.floating-save-button.has-changes:hover {
  transform: translateY(-4px);
  box-shadow:
    0 25px 80px rgba(0, 0, 0, 0.5),
    0 15px 50px rgba(68, 108, 156, 0.4),
    0 0 0 1px rgba(253, 253, 253, 0.2),
    0 0 30px rgba(68, 108, 156, 0.4);
}

.floating-save-button.saving {
  background: linear-gradient(135deg, var(--brand-aurora-blue), var(--brand-lumen-green));
  color: var(--brand-lamp-white);
  cursor: not-allowed;
  opacity: 0.8;
}

.floating-save-button:disabled {
  cursor: not-allowed;
}

@media (max-width: 479px) {
  .floating-save-container {
    left: 16px;
    right: 16px;
    transform: none;
    padding: 0 16px;
  }

  .floating-save-button {
    width: 100%;
    max-width: 300px;
    min-width: auto;
  }
}
</style>
