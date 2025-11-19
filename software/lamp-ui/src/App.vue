<script setup lang="ts">
import { ref, onMounted, onUnmounted, computed, watch } from 'vue'

import ColorGradient from '@/components/ColorGradient.vue'
import BrightnessSlider from '@/components/BrightnessSlider.vue'
import NumberInput from '@/components/NumberInput.vue'
import TextInput from '@/components/TextInput.vue'
import BooleanInput from '@/components/BooleanInput.vue'
import FormField from '@/components/FormField.vue'
import TopNavigation from '@/components/TopNavigation.vue'
import Logo from '@/components/Logo.vue'
import Nameplate from '@/components/Nameplate.vue'
import ExpressionsList from '@/components/expressions/ExpressionsList.vue'
import type { Settings } from '@/types'

// Additional type definitions for expressions
interface Expression {
  type: string
  enabled: boolean
  colors: string[]
  intervalMin: number
  intervalMax: number
  target: number
  duration?: number
  durationMin?: number
  durationMax?: number
}

// Extend Settings interface
declare module '@/types' {
  interface Settings {
    expressions?: Expression[]
  }
}

// configuration ==============

const maxReconnectAttempts = 60
const reconnectInterval = 2500
const websocketDebounceInterval = 10
const maxLedsShade = 38
const maxLedsBase = 50

// state ======================

const settings = ref<Settings>({})
const loaded = ref(false)
const disabled = ref(false)
const originalSettings = ref<string>('')
const saving = ref(false)
const resetUnsavedChanges = ref(0)
const activeTab = ref('home')

// Tab configuration
const tabs = [
  { id: 'home', label: 'Home' },
  { id: 'expressions', label: 'Expressions' },
  { id: 'lamp-setup', label: 'Setup' },
  { id: 'info', label: 'Info' },
]

// Send tab state when it changes
watch(activeTab, (newTab) => {
  // Tell the lamp what tab is active
  if (ws.value?.readyState === WebSocket.OPEN) {
    websocketSend({ a: 'tab', v: newTab })
  }
})

const ws = ref<WebSocket | null>(null)
const wsConnected = ref(false)
const reconnectAttempts = ref(0)
let reconnectTimeout: number | null = null
let websocketDebounceTimeout: number | null = null

// Computed property to check if settings have changed
const hasChanges = computed(() => {
  return JSON.stringify(settings.value) !== originalSettings.value
})

// Generic function to update settings -- except knockout pixels
const updateSetting = (path: string, value: unknown) => {
  const pathParts = path.split('.')
  let current: Record<string, unknown> = settings.value

  for (let i = 0; i < pathParts.length - 1; i++) {
    if (!current[pathParts[i]]) {
      current[pathParts[i]] = {}
    }
    current = current[pathParts[i]] as Record<string, unknown>
  }

  const finalKey = pathParts[pathParts.length - 1]
  current[finalKey] = value

  let action: Record<string, unknown> | undefined
  switch (path) {
    case 'lamp.brightness':
      // If home mode is OFF, apply this brightness immediately
      if (!settings.value.lamp?.homeMode) {
        action = { a: 'bright', v: value }
      }
      break
    case 'lamp.homeModeBrightness':
      // If home mode is ON, apply this brightness immediately
      if (settings.value.lamp?.homeMode) {
        action = { a: 'bright', v: value }
      }
      break
    case 'lamp.homeMode':
      // When toggling home mode, apply the appropriate brightness
      if (value) {
        // Turning ON: apply home mode brightness
        action = { a: 'bright', v: settings.value.lamp?.homeModeBrightness ?? 80 }
      } else {
        // Turning OFF: apply regular brightness
        action = { a: 'bright', v: settings.value.lamp?.brightness || 100 }
      }
      break
    case 'shade.colors':
      action = { a: 'shade', c: value }
      break
    case 'base.colors':
      action = { a: 'base', c: value }
      break
    // Don't send real-time updates for expressions
    // They are cleared when entering tab and reloaded when leaving
  }
  if (action) {
    websocketSend(action)
  }
}

const updateKnockoutPixel = (ledIndex: number, brightness: number) => {
  if (!settings.value.base) {
    settings.value.base = {}
  }
  if (!settings.value.base) {
    settings.value.base = {}
  }
  if (!settings.value.base?.knockout) {
    settings.value.base.knockout = []
  }

  const existingIndex = settings.value.base.knockout.findIndex((kp) => kp.p === ledIndex)

  if (brightness === 100) {
    if (existingIndex !== -1) {
      settings.value.base.knockout.splice(existingIndex, 1)
    }
  } else {
    if (existingIndex !== -1) {
      settings.value.base.knockout[existingIndex].b = brightness
    } else {
      settings.value.base.knockout.push({ p: ledIndex, b: brightness })
    }
  }

  const action = { a: 'knockout', p: ledIndex, b: brightness }
  websocketSend(action)
}

// Get brightness for a specific LED (returns 100 if not in knockout array)
const getKnockoutBrightness = (ledIndex: number): number => {
  if (!settings.value.base?.knockout) return 100
  const knockout = settings.value.base.knockout.find((kp) => kp.p === ledIndex)
  return knockout ? knockout.b : 100
}

const saveSettings = async () => {
  if (!hasChanges.value || saving.value) return

  saving.value = true

  // apply an extra filter on settings.value.base.knockout to remove any empty objects and knockout pixels values that are 100
  if (!settings.value.base) {
    settings.value.base = {}
  }
  settings.value.base.knockout =
    settings.value.base?.knockout?.filter(({ p, b }) => p !== undefined && p !== null && b < 100) ??
    []

  try {
    const response = await fetch(`${import.meta.env.VITE_SERVER_HTTP}/settings`, {
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify(settings.value),
    })

    if (response.ok) {
      originalSettings.value = JSON.stringify(settings.value)
    }
    saving.value = false
  } catch (error) {
    console.error('Error saving settings:', error)
  } finally {
    saving.value = false
  }
}

function connectWebSocket() {
  if (reconnectTimeout) {
    clearTimeout(reconnectTimeout)
    reconnectTimeout = null
  }

  if (ws.value) {
    ws.value.close()
  }

  ws.value = new WebSocket(`${import.meta.env.VITE_SERVER_WS}`)

  ws.value.onopen = () => {
    wsConnected.value = true
    disabled.value = false
    reconnectAttempts.value = 0

    // Send current brightness based on home mode state
    if (settings.value.lamp) {
      const brightness = settings.value.lamp.homeMode
        ? (settings.value.lamp.homeModeBrightness ?? 80)
        : (settings.value.lamp.brightness ?? 100)
      websocketSend({ a: 'bright', v: brightness })
    }

    // Send current tab state
    websocketSend({ a: 'tab', v: activeTab.value })

    // Send current colors to establish preview
    if (settings.value.shade?.colors) {
      websocketSend({ a: 'shade', c: settings.value.shade.colors })
    }
    if (settings.value.base?.colors) {
      websocketSend({ a: 'base', c: settings.value.base.colors })
    }

    ws.value?.send(
      JSON.stringify({
        type: 'test',
        message: 'Hello WebSocket!',
        timestamp: new Date().toISOString(),
      }),
    )
  }

  ws.value.onclose = () => {
    wsConnected.value = false
    disabled.value = true
    if (reconnectAttempts.value < maxReconnectAttempts) {
      reconnectAttempts.value++
      reconnectTimeout = window.setTimeout(() => {
        connectWebSocket()
      }, reconnectInterval)
    } else {
      console.log('Max reconnection attempts reached. Stopping reconnection attempts.')
    }
  }

  ws.value.onerror = (error) => {
    console.error('WebSocket error:', error)
    wsConnected.value = false
    disabled.value = true
  }

  return ws.value
}

const websocketSend = (action: Record<string, unknown>) => {
  // Clear any existing debounce timeout
  if (websocketDebounceTimeout) {
    clearTimeout(websocketDebounceTimeout)
  }

  // Set a new timeout to send the message after 25ms
  websocketDebounceTimeout = window.setTimeout(() => {
    ws.value?.send(JSON.stringify(action))
    websocketDebounceTimeout = null
  }, websocketDebounceInterval)
}

const handleTestExpression = (type: string) => {
  const action = { a: 'test_expression', type }
  websocketSend(action)
}

const handleTestExpressionComplete = () => {
  // Re-enable configurator and restore preview colors
  const action = {
    a: 'test_expression_complete',
    shadeColors: settings.value.shade?.colors || [],
    baseColors: settings.value.base?.colors || [],
  }
  websocketSend(action)
}

const handleExpressionColorPreview = (color: string, target: number) => {
  // Send single color as array for solid color preview
  if (target === 1 || target === 3) {
    // Shade or Both
    websocketSend({ a: 'shade', c: [color] })
  }
  if (target === 2 || target === 3) {
    // Base or Both
    websocketSend({ a: 'base', c: [color] })
  }
}

const handleExpressionColorPickerOpen = () => {
  // Configurator stays enabled, expression color will override temporarily
}

const handleExpressionColorPickerClose = () => {
  // Restore the colors from the colors tab settings
  if (settings.value.shade?.colors) {
    websocketSend({ a: 'shade', c: settings.value.shade.colors })
  }
  if (settings.value.base?.colors) {
    websocketSend({ a: 'base', c: settings.value.base.colors })
  }
}

// Removed - configurator now uses 60-second timeout instead of explicit stop

const handleSaveAndRestart = async () => {
  await saveSettings()
  // Increment to trigger reset in ExpressionsList component
  resetUnsavedChanges.value++
}

onMounted(async () => {
  const response = await fetch(`${import.meta.env.VITE_SERVER_HTTP}/settings`)
  const data = await response.json()
  settings.value = data
  originalSettings.value = JSON.stringify(data)
  loaded.value = true
  connectWebSocket()
})

onUnmounted(() => {
  // Clean up WebSocket connection and reconnection timeout
  if (reconnectTimeout) {
    clearTimeout(reconnectTimeout)
    reconnectTimeout = null
  }

  // Clean up websocket debounce timeout
  if (websocketDebounceTimeout) {
    clearTimeout(websocketDebounceTimeout)
    websocketDebounceTimeout = null
  }

  if (ws.value) {
    ws.value.close()
    ws.value = null
  }

  wsConnected.value = false
  disabled.value = true
})
</script>

<template>
  <div class="home">
    <!-- WebSocket Status Indicator -->
    <div
      class="ws-status-indicator"
      :class="{ connected: wsConnected }"
      :title="wsConnected ? 'WebSocket Connected' : 'WebSocket Disconnected'"
    >
      <div class="ws-status-dot"></div>
    </div>

    <div v-if="loaded" class="container">
      <main class="main-content">
        <!-- Tab Navigation -->
        <TopNavigation
          :tabs="tabs"
          :active-tab="activeTab"
          @update:active-tab="activeTab = $event"
        />

        <!-- Tab Content -->
        <div class="tab-content">
          <!-- Home Tab -->
          <section v-if="activeTab === 'home'" class="tab-panel" aria-label="Home settings">
            <Nameplate v-model="settings" id="nameplate" />

            <h1 class="gold">Lamp Brightness</h1>
            <FormField id="brightness">
              <BrightnessSlider
                :model-value="settings.lamp?.brightness || 0"
                @update:model-value="(value) => updateSetting('lamp.brightness', value)"
                id="brightness"
                :min="0"
                :max="100"
                append="%"
                :disabled="disabled || settings.lamp?.homeMode"
              />
            </FormField>

            <h1 class="yellow">Lamp Color Settings</h1>
            <FormField label="Shade" id="shadeColors">
              <ColorGradient
                :model-value="settings.shade?.colors || ['#FF0000FF']"
                @update:model-value="(value) => updateSetting('shade.colors', value)"
                :show-add-button="false"
                :max-colors="1"
                :disabled="disabled"
              />
            </FormField>

            <FormField label="Base" id="baseColors">
              <ColorGradient
                :model-value="settings.base?.colors || ['#FF0000FF']"
                @update:model-value="(value) => updateSetting('base.colors', value)"
                :disabled="disabled"
                :active-color="settings.base?.ac || 0"
                @update:active-color="(value) => updateSetting('base.ac', value)"
              />
            </FormField>
          </section>

          <!-- Expressions Tab -->
          <section
            v-if="activeTab === 'expressions'"
            class="tab-panel"
            aria-label="Expression settings"
          >
            <div class="expressions-instructions">
              <p>
                Add expressions to give your lamp personality. Expressions are behaviors that
                trigger randomly to create visual effects.
              </p>
            </div>
            <ExpressionsList
              :model-value="settings.expressions || []"
              @update:model-value="(value) => updateSetting('expressions', value)"
              @test-expression="handleTestExpression"
              @test-expression-complete="handleTestExpressionComplete"
              @save-and-restart="handleSaveAndRestart"
              @preview-color="handleExpressionColorPreview"
              @color-picker-open="handleExpressionColorPickerOpen"
              @color-picker-close="handleExpressionColorPickerClose"
              :reset-unsaved-changes="resetUnsavedChanges"
              :disabled="disabled"
            />
          </section>

          <!-- Lamp Setup Tab -->
          <section v-if="activeTab === 'lamp-setup'" class="tab-panel" aria-label="Setup settings">
            <h1 class="gold">Lamp Name</h1>
            <FormField id="name">
              <TextInput
                :model-value="settings.lamp?.name || ''"
                @update:model-value="(value) => updateSetting('lamp.name', value)"
                placeholder="Enter a name for your lamp"
                :disabled="disabled"
                :max-length="12"
                pattern="[a-z]+"
                transform="lowercase"
              />
              <div class="password-info-text">
                Names must be all lowercase letters and between 3-12 characters.
              </div>
            </FormField>

            <h1 class="yellow">Lamp Password</h1>
            <FormField id="password">
              <TextInput
                :model-value="settings.lamp?.password || ''"
                @update:model-value="(value) => updateSetting('lamp.password', value)"
                placeholder="Optional password"
                :disabled="disabled"
                pattern="[ -~]+"
                :max-length="16"
              />
              <div class="password-info-text">
                Optional password to protect your lamp from changes. Between 8-16 characters. Leave
                empty for no password.
              </div>
            </FormField>

            <h1 class="lime">At-Home Mode</h1>
            <div class="mode-toggles">
              <FormField label="Home Mode" id="homeMode">
                <BooleanInput
                  :model-value="settings.lamp?.homeMode || false"
                  @update:model-value="(value) => updateSetting('lamp.homeMode', value)"
                  :disabled="disabled"
                />
              </FormField>

              <!-- Home Mode Settings -->
              <div v-if="settings.lamp?.homeMode" class="home-mode-settings">
                <FormField label="Home Mode Brightness" id="homeModeBrightness">
                  <BrightnessSlider
                    :model-value="settings.lamp?.homeModeBrightness ?? 80"
                    @update:model-value="(value) => updateSetting('lamp.homeModeBrightness', value)"
                    id="homeModeBrightness"
                    :min="0"
                    :max="100"
                    append="%"
                    :disabled="disabled"
                  />
                </FormField>

                <FormField label="Home Network SSID" id="homeModeSSID">
                  <TextInput
                    :model-value="settings.lamp?.homeModeSSID || ''"
                    @update:model-value="(value) => updateSetting('lamp.homeModeSSID', value)"
                    placeholder="Enter your home WiFi name"
                    :disabled="disabled"
                    :max-length="32"
                    pattern="[ -~]+"
                  />
                  <div id="home-ssid-info" class="info-text">
                    When the lamp detects this WiFi network, it will automatically activate special
                    home-only features and behaviors.
                  </div>
                </FormField>
              </div>
            </div>

            <h1 class="green">Lamp Base LED Profile</h1>
            <FormField label="Base LED Count" id="baseLeds">
              <NumberInput
                :model-value="settings.base?.px || 36"
                @update:model-value="(value) => updateSetting('base.px', value)"
                :min="5"
                :max="maxLedsBase"
                placeholder="Number of LEDs"
                :disabled="disabled"
              />
            </FormField>

            <FormField label="Per-Pixel Brightness Adjustment" id="baseKnockoutPixels" expandable>
              <div class="pixel-grid">
                <div
                  v-for="ledIndex in Array.from(
                    { length: settings.base?.px || 36 },
                    (_, i) => (settings.base?.px || 36) - i,
                  )"
                  :key="ledIndex - 1"
                  class="pixel-row"
                >
                  <label class="pixel-label">LED {{ ledIndex }}</label>
                  <BrightnessSlider
                    :model-value="getKnockoutBrightness(ledIndex - 1)"
                    @update:model-value="(value) => updateKnockoutPixel(ledIndex - 1, value)"
                    :id="`knockout-pixel-${ledIndex - 1}`"
                    :min="0"
                    :max="100"
                    append="%"
                    :disabled="disabled"
                  />
                </div>
              </div>
            </FormField>
          </section>

          <!-- Information Tab -->
          <section v-if="activeTab === 'info'" class="tab-panel" aria-label="Information">
            <div class="info-content">
              <div class="logo-container">
                <Logo />
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
    <div v-if="loaded" class="floating-save-container">
      <button
        class="floating-save-button"
        :class="{
          'has-changes': hasChanges,
          saving: saving,
          'no-changes': !hasChanges || disabled,
        }"
        @click="saveSettings"
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

/* Prevent zooming on mobile devices for all interactive elements */
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
  -khtml-user-select: none;
  -moz-user-select: none;
  -ms-user-select: none;
  user-select: none;
}

/* Allow text selection in input fields and textareas */
input[type='text'],
input[type='email'],
input[type='password'],
input[type='search'],
input[type='url'],
input[type='tel'],
textarea {
  -webkit-user-select: text;
  -khtml-user-select: text;
  -moz-user-select: text;
  -ms-user-select: text;
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

/* Tab Content Styles */
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

.empty-state {
  color: var(--brand-slate-grey);
  font-style: italic;
  text-align: center;
  padding: 40px 20px;
  background: rgba(253, 253, 253, 0.02);
  border-radius: 8px;
  border: 1px dashed var(--color-border);
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

.logo {
  width: 200px;
  max-width: 40%;
  height: auto;
  border-radius: 8px;
}

.info-content h2 {
  color: var(--brand-fog-grey);
  margin-bottom: 16px;
  font-size: 1.5rem;
  font-weight: 600;
}

.info-content h3 {
  color: var(--brand-fog-grey);
  margin: 24px 0 12px 0;
  font-size: 1.1rem;
  font-weight: 500;
}

.info-content p {
  line-height: 1.6;
  margin-bottom: 16px;
}

.info-content ul {
  margin: 0 0 16px 0;
  padding-left: 20px;
}

.info-content li {
  margin-bottom: 8px;
  line-height: 1.5;
}

.connection-status {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 16px;
}

.status-indicator {
  width: 12px;
  height: 12px;
  border-radius: 50%;
  display: inline-block;
}

.status-indicator.connected {
  background: #4ade80;
  box-shadow: 0 0 8px rgba(74, 222, 128, 0.4);
}

.status-indicator.disconnected {
  background: #ef4444;
  box-shadow: 0 0 8px rgba(239, 68, 68, 0.4);
}

/* Mobile-first design - always mobile-like */
@media (min-width: 480px) {
  .container {
    max-width: 400px;
  }

  .home {
    padding: 20px;
  }

  .main-content {
    padding: 20px;
    padding-bottom: 40px !important;
  }
}

@media (min-width: 1024px) {
  .container {
    max-width: 450px;
  }
}

/* Mode Toggles Styles */
.mode-toggles {
  display: flex;
  flex-direction: column;
  gap: 0;
}

.mode-toggles > .form-field {
  width: 100%;
}

/* Home Mode SSID Styles */
.home-mode-settings {
  animation: fadeIn 0.3s ease-in-out;
}

.home-mode-settings .form-field {
  margin-top: 8px;
  margin-bottom: 32px;
}

.home-mode-settings .info-text {
  margin-top: 12px;
  padding: 8px 12px;
  background: rgba(68, 108, 156, 0.08);
  border-left: 2px solid var(--brand-aurora-blue);
  border-radius: 4px;
  font-size: 0.75rem;
  line-height: 1.4;
  color: var(--brand-slate-grey);
}

/* Tab Instructions */
.colors-instructions,
.home-instructions,
.setup-instructions,
.expressions-instructions {
  margin-bottom: 24px;
  padding: 12px 16px;
  background: rgba(68, 108, 156, 0.06);
  border-radius: 6px;
}

.colors-instructions p,
.home-instructions p,
.setup-instructions p,
.expressions-instructions p {
  margin: 0;
  font-size: 0.85rem;
  line-height: 1.5;
  color: var(--brand-fog-grey);
}

/* Password Info Text */
.password-info-text {
  margin-top: 12px;
  padding: 8px 12px;
  background: rgba(68, 108, 156, 0.08);
  border-left: 2px solid var(--brand-aurora-blue);
  border-radius: 4px;
  font-size: 0.75rem;
  line-height: 1.4;
  color: var(--brand-slate-grey);
}

/* Knockout Pixels Styles */

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

.pixel-row .number-slider {
  flex: 1;
}

/* Floating Save Button Styles */
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

/* Mobile adjustments */
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
