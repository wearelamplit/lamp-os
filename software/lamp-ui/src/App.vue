<script setup lang="ts">
import { ref, computed, onMounted, onUnmounted, watch } from 'vue'

import TopNavigation from './components/TopNavigation.vue'
import LamplitLogo from './components/LamplitLogo.vue'
import CritterNameplate from './components/CritterNameplate.vue'
import FormField from './components/FormField.vue'
import BrightnessSlider from './components/BrightnessSlider.vue'
import ColorGradient from './components/ColorGradient.vue'
import TextInput from './components/TextInput.vue'
import BooleanInput from './components/BooleanInput.vue'
import NumberInput from './components/NumberInput.vue'
import ExpressionsList from './components/expressions/ExpressionsList.vue'
import type { Config } from './types'

// Firmware does a full-replace on PUT: any field omitted from the body resets
// to its firmware default on the post-save reboot. So we GET the whole doc,
// mutate only the editable fields in place, and PUT the entire doc back.
const defaultConfig = (): Config => ({
  lamp: {
    name: '',
    brightness: 100,
    setup: true,
    colorsRandomized: false,
    advancedEnabled: false,
    devMode: false,
    webappEnabled: true,
    socialMode: 1,
  },
  base: { px: 0, ac: 0, bpp: 4, byteOrder: 'GRBW', colors: ['#FF0000FF'], knockout: [] },
  shade: { px: 0, bpp: 4, byteOrder: 'GRBW', colors: ['#FF0000FF'] },
  expressions: [],
  homeMode: { ssid: '', brightness: 60, enabled: false },
})

const tabs = [
  { id: 'home', label: 'Home' },
  { id: 'expressions', label: 'Expressions' },
  { id: 'lamp-setup', label: 'Setup' },
  { id: 'info', label: 'Info' },
]

const cfg = ref<Config>(defaultConfig())
const ready = ref(false)
const saving = ref(false)
const activeTab = ref('home')
const wsConnected = ref(false)

// Password is write-only: GET never returns it, so this starts empty. Only a
// non-empty value is injected into the PUT body; empty keeps the existing one.
const password = ref('')

// Baseline for change detection; password lives outside cfg so track it too.
const originalSettings = ref('')
const hasChanges = computed(
  () => JSON.stringify(cfg.value) !== originalSettings.value || password.value !== '',
)

const disabled = computed(() => !wsConnected.value)

let ws: WebSocket | null = null
let reconnectTimer: ReturnType<typeof setTimeout> | null = null

const sendPreview = (payload: Record<string, string[] | number>) => {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload))
  }
}

// Re-assert the saved base/shade/brightness on the lamp — used to undo an
// expression-color preview when leaving the expressions tab.
const restoreColorPreview = () => {
  if (!ready.value) return
  sendPreview({ baseColors: [...cfg.value.base.colors] })
  sendPreview({ shadeColors: [...cfg.value.shade.colors] })
  sendPreview({ brightness: cfg.value.lamp.brightness })
}

// Light the lamp with an expression's palette while its colors are edited.
// The firmware has no expression-color preview path, so we borrow the flat
// baseColors preview; restoreColorPreview() puts the real base back.
const previewExpressionColors = (colors: string[]) => {
  if (ready.value && colors.length) sendPreview({ baseColors: [...colors] })
}

const connectWs = () => {
  ws = new WebSocket(`ws://${location.host}/ws`)
  ws.onopen = () => {
    wsConnected.value = true
    // Re-establish live preview after a (re)connect — e.g. post-save reboot.
    sendPreview({ baseColors: [...cfg.value.base.colors] })
    sendPreview({ shadeColors: [...cfg.value.shade.colors] })
    sendPreview({ brightness: cfg.value.lamp.brightness })
  }
  ws.onclose = () => {
    wsConnected.value = false
    reconnectTimer = setTimeout(connectWs, 2000)
  }
}

// Live preview is FLAT and only covers base/shade colors + brightness.
// Knockout / expressions / home mode apply on save+reboot — no preview.
watch(
  () => cfg.value.base.colors,
  (value) => ready.value && sendPreview({ baseColors: [...value] }),
  { deep: true },
)

watch(
  () => cfg.value.shade.colors,
  (value) => ready.value && sendPreview({ shadeColors: [...value] }),
  { deep: true },
)

watch(
  () => cfg.value.lamp.brightness,
  (value) => ready.value && sendPreview({ brightness: value }),
)

// Leaving the expressions tab undoes any lingering expression-color preview.
watch(activeTab, (_tab, prev) => {
  if (prev === 'expressions') restoreColorPreview()
})

// Keep the dense per-pixel knockout array sized to the base pixel count.
watch(
  () => cfg.value.base.px,
  (px) => {
    if (!ready.value) return
    const k = cfg.value.base.knockout
    if (px > k.length) while (k.length < px) k.push(100)
    else if (px < k.length) k.length = px
  },
)

const save = async () => {
  if (!hasChanges.value || saving.value) return
  saving.value = true
  if (password.value) cfg.value.lamp.password = password.value
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
      originalSettings.value = JSON.stringify(cfg.value)
    }
  } catch {
    // leave hasChanges true so the user can retry
  } finally {
    saving.value = false
  }
}

onMounted(async () => {
  try {
    const res = await fetch('/api/settings')
    const data = await res.json()
    if (data && data.lamp) {
      cfg.value = data as Config
      // ColorPicker / ColorGradient need at least one color to bind.
      if (!cfg.value.base.colors?.length) cfg.value.base.colors = ['#FF0000FF']
      if (!cfg.value.shade.colors?.length) cfg.value.shade.colors = ['#FF0000FF']
      if (!Array.isArray(cfg.value.base.knockout)) cfg.value.base.knockout = []
      if (!Array.isArray(cfg.value.expressions)) cfg.value.expressions = []
    }
  } catch {
    // Leave defaults; user can still save fresh values.
  }
  originalSettings.value = JSON.stringify(cfg.value)
  ready.value = true
  connectWs()
})

onUnmounted(() => {
  if (ws) {
    ws.onclose = null
    ws.close()
  }
  if (reconnectTimer) clearTimeout(reconnectTimer)
})
</script>

<template>
  <div class="home">
    <div
      class="ws-status-indicator"
      :class="{ connected: wsConnected }"
      :title="wsConnected ? 'Connected' : 'Disconnected'"
    >
      <div class="ws-status-dot"></div>
    </div>

    <div v-if="ready" class="container">
      <main class="main-content">
        <TopNavigation :tabs="tabs" :active-tab="activeTab" @update:active-tab="activeTab = $event" />

        <div class="tab-content">
          <!-- Home -->
          <section v-if="activeTab === 'home'" class="tab-panel" aria-label="Home settings">
            <CritterNameplate :name="cfg.lamp.name" />

            <h1 class="gold">Lamp Brightness</h1>
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

            <h1 class="yellow">Lamp Color Settings</h1>
            <FormField label="Shade" id="shadeColors">
              <ColorGradient
                v-model="cfg.shade.colors"
                :show-add-button="false"
                :max-colors="1"
                :disabled="disabled"
              />
            </FormField>

            <FormField label="Base" id="baseColors">
              <ColorGradient
                v-model="cfg.base.colors"
                :max-colors="5"
                :disabled="disabled"
                :active-color="cfg.base.ac"
                @update:active-color="(v) => (cfg.base.ac = v)"
              />
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
              :disabled="disabled"
              @preview="previewExpressionColors"
            />
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
              />
              <div class="info-text">Up to 12 characters.</div>
            </FormField>

            <h1 class="yellow">Lamp Password</h1>
            <FormField id="password">
              <TextInput
                v-model="password"
                placeholder="Leave unchanged"
                :disabled="disabled"
                :max-length="16"
              />
              <div class="info-text">
                Optional password to protect your lamp from changes. Leave empty to keep the current
                password.
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

                <FormField label="Home Network SSID" id="homeModeSSID">
                  <TextInput
                    v-model="cfg.homeMode.ssid"
                    placeholder="Enter your home WiFi name"
                    :disabled="disabled"
                    :max-length="32"
                  />
                  <div class="info-text">
                    When the lamp detects this WiFi network, it activates home-only behaviors.
                  </div>
                </FormField>
              </div>
            </div>

            <h1 class="green">Lamp Base LED Profile</h1>
            <FormField label="Base LED Count" id="baseLeds">
              <NumberInput
                v-model="cfg.base.px"
                :min="1"
                :max="200"
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
          </section>

          <!-- Info -->
          <section v-if="activeTab === 'info'" class="tab-panel" aria-label="Information">
            <div class="info-content">
              <div class="logo-container">
                <LamplitLogo />
              </div>
              <p>
                Lamplit Art Society is a non-profit collective dedicated to sparking inspiration and
                providing opportunities for people to connect, celebrate, grow, and inspire others
                through shared creative experiences.
              </p>
              <p>
                The lamps are the art project from which our society grew. Their surreal and vivid
                presence captivates audiences, fosters unexpected connections, inspires creativity
                and play, and illuminates spaces.
              </p>
              <p>
                As stewards of this decentralized and open source project, we maintain its core
                vision while welcoming contributors and artists to build, adopt, or share these
                lamps with their communities.
              </p>
              <p>Find more info at <b>lamplit.ca</b></p>
            </div>
          </section>
        </div>
      </main>
    </div>

    <!-- Floating Save Button -->
    <div v-if="ready" class="floating-save-container">
      <button
        class="floating-save-button"
        :class="{
          'has-changes': hasChanges && !disabled,
          saving: saving,
          'no-changes': !hasChanges || disabled,
        }"
        @click="save"
        :disabled="!hasChanges || saving || disabled"
      >
        <span v-if="disabled">Connecting...</span>
        <span v-else-if="saving">Saving...</span>
        <span v-else-if="hasChanges">Save Changes</span>
        <span v-else>No Changes</span>
      </button>
    </div>
  </div>
</template>

<style>
#app {
  min-height: 100vh;
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
  min-height: 100vh;
  background: var(--brand-midnight-black);
  padding: 16px;
  padding-bottom: 10px !important;
  width: 100%;
}

.container {
  width: 100%;
  max-width: 100%;
  margin: 0 auto;
}

.main-content {
  background: var(--color-background-soft);
  border-radius: 16px;
  padding: 20px;
  padding-bottom: 40px !important;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.3);
  backdrop-filter: blur(10px);
}

.tab-content {
  min-height: 200px;
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

.info-content {
  padding: 20px;
  color: var(--brand-slate-grey);
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

.info-content p {
  line-height: 1.6;
  margin-bottom: 16px;
}

@media (min-width: 480px) {
  .container {
    max-width: 400px;
  }

  .home {
    padding: 20px;
  }
}

@media (min-width: 1024px) {
  .container {
    max-width: 450px;
  }
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
  bottom: 15px;
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

/* WebSocket Status Indicator */
.ws-status-indicator {
  position: fixed;
  bottom: 16px;
  right: 16px;
  z-index: 1001;
  background: rgba(0, 0, 0, 0.6);
  border-radius: 50%;
  backdrop-filter: blur(10px);
  transition: all 0.3s ease;
}

.ws-status-dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--color-error);
  transition: all 0.3s ease;
  box-shadow: 0 0 8px rgba(248, 113, 113, 0.5);
}

.ws-status-indicator.connected .ws-status-dot {
  background: var(--color-success);
  box-shadow: 0 0 8px rgba(141, 205, 166, 0.5);
}

@media (max-width: 479px) {
  .floating-save-container {
    bottom: 16px;
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

  .ws-status-indicator {
    bottom: 12px;
    right: 12px;
  }
}
</style>
