<script setup lang="ts">
import { ref, computed } from 'vue'
import ComponentForm from '@/components/Form.vue'
import BrightnessSlider from '@/components/BrightnessSlider.vue'
import type { FieldDefinition, FormValues } from '@/types'
import { useLampStore, MAX_LEDS_BASE } from '@/stores/lamp'

const lampStore = useLampStore()

// File input ref for restore functionality
const fileInputRef = ref<HTMLInputElement | null>(null)
const restoreError = ref<string | null>(null)
const restoreSuccess = ref(false)

// Export entire lamp state as JSON file download
const handleExport = () => {
  if (lampStore.hasChanges) return

  const jsonData = lampStore.exportState()
  const blob = new Blob([jsonData], { type: 'application/json' })
  const url = URL.createObjectURL(blob)

  const lampName = lampStore.state.lamp?.name || 'unnamed'
  const timestamp = Date.now()
  const filename = `lamp-${lampName}-${timestamp}.json`

  const link = document.createElement('a')
  link.href = url
  link.download = filename
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  URL.revokeObjectURL(url)
}

// Trigger file input for restore
const handleRestoreClick = () => {
  if (lampStore.hasChanges) return
  restoreError.value = null
  restoreSuccess.value = false
  fileInputRef.value?.click()
}

// Handle file selection and parse JSON locally
const handleFileSelect = (event: Event) => {
  const target = event.target as HTMLInputElement
  const file = target.files?.[0]

  if (!file) return

  const reader = new FileReader()
  reader.onload = (e) => {
    const content = e.target?.result as string
    const result = lampStore.restoreState(content)

    if (result.success) {
      restoreSuccess.value = true
      restoreError.value = null
    } else {
      restoreError.value = result.error || 'Failed to restore backup'
      restoreSuccess.value = false
    }

    // Reset file input so same file can be selected again
    if (fileInputRef.value) {
      fileInputRef.value.value = ''
    }
  }

  reader.onerror = () => {
    restoreError.value = 'Failed to read file'
    restoreSuccess.value = false
  }

  reader.readAsText(file)
}

// Field definitions for the setup page form
const fields = ref<FieldDefinition[]>([
  {
    name: 'lampNameHeading',
    type: 'group-heading',
    label: 'Lamp Name',
  },
  {
    name: 'name',
    type: 'text',
    help: 'Names must be all lowercase letters and between 3-12 characters.',
    default: '',
    optional: true,
    props: {
      placeholder: 'Enter a name for your lamp',
      maxLength: 12,
      minLength: 3,
      pattern: '[a-z]+',
      transform: 'lowercase',
    },
  },
  {
    name: 'passwordHeading',
    type: 'group-heading',
    label: 'Lamp Password',
  },
  {
    name: 'password',
    type: 'password',
    help: 'Optional password to protect your lamp from changes. Between 8-16 characters. Leave empty for no password.',
    default: '',
    optional: true,
    props: {
      placeholder: 'Optional password',
      maxLength: 16,
      minLength: 8,
    },
  },
  {
    name: 'homeModeHeading',
    type: 'group-heading',
    label: 'At-Home Mode',
  },
  {
    name: 'homeMode',
    type: 'boolean',
    label: 'Home Mode',
    default: false,
    optional: true,
  },
  {
    name: 'homeModeBrightness',
    type: 'brightness-slider',
    label: 'Home Mode Brightness',
    default: 80,
    optional: true,
    // show: (values: FormValues) => values.homeMode === true,
    props: {
      min: 0,
      max: 100,
      step: 1,
      append: '%',
    },
  },
  {
    name: 'homeModeSSID',
    type: 'text',
    label: 'Home Network SSID',
    help: 'When the lamp detects this WiFi network, it will automatically activate special home-only features and behaviors.',
    default: '',
    optional: true,
    show: (values: FormValues) => values.homeMode === true,
    props: {
      placeholder: 'Enter your home WiFi name',
      maxLength: 32,
    },
  },
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
    props: {
      min: 5,
      max: MAX_LEDS_BASE,
      placeholder: 'Number of LEDs',
    },
  },
  {
    name: 'knockoutPixels',
    type: 'slot',
    label: 'Per-Pixel Brightness Adjustment',
  },
  {
    name: 'backupHeading',
    type: 'group-heading',
    label: 'Backup & Restore',
  },
  {
    name: 'backupControls',
    type: 'slot',
  },
])

// Map store state to form values
const formValues = computed({
  get: () => ({
    name: lampStore.state.lamp?.name ?? '',
    password: lampStore.state.lamp?.password ?? '',
    homeMode: lampStore.state.lamp?.homeMode ?? false,
    homeModeBrightness: lampStore.state.lamp?.homeModeBrightness ?? 80,
    homeModeSSID: lampStore.state.lamp?.homeModeSSID ?? '',
    basePx: lampStore.state.base?.px ?? 36,
  }),
  set: () => {
    // Values are updated via individual handlers
  },
})

// Handle form value changes
const handleFormUpdate = (values: FormValues) => {
  if (values.name !== undefined && values.name !== formValues.value.name) {
    lampStore.updateLampName(values.name as string)
  }
  if (values.password !== undefined && values.password !== formValues.value.password) {
    lampStore.updateLampPassword(values.password as string)
  }
  if (values.homeMode !== undefined && values.homeMode !== formValues.value.homeMode) {
    lampStore.updateHomeMode(values.homeMode as boolean)
  }
  if (values.homeModeBrightness !== undefined && values.homeModeBrightness !== formValues.value.homeModeBrightness) {
    lampStore.updateHomeModeBrightness(values.homeModeBrightness as number)
  }
  if (values.homeModeSSID !== undefined && values.homeModeSSID !== formValues.value.homeModeSSID) {
    lampStore.updateHomeModeSSID(values.homeModeSSID as string)
  }
  if (values.basePx !== undefined && values.basePx !== formValues.value.basePx) {
    lampStore.updateBasePxCount(values.basePx as number)
  }
}

// LED pixel count for knockout grid
const ledCount = computed(() => lampStore.state.base?.px ?? 36)
</script>

<template>
  <section class="tab-panel" aria-label="Setup settings">
    <ComponentForm
      :fields="fields"
      :model-value="formValues"
      @update:model-value="handleFormUpdate"
      :show-button="false"
      :disabled="lampStore.disabled"
    >
      <!-- Per-Pixel Brightness Adjustment slot -->
      <template #knockoutPixels>
        <CollapsiblePanel label="Per-Pixel Brightness Adjustment">
          <div class="pixel-grid">
            <div
              v-for="ledIndex in Array.from(
                { length: ledCount },
                (_, i) => ledCount - i,
              )"
              :key="ledIndex - 1"
              class="pixel-row"
            >
              <label class="pixel-label">LED {{ ledIndex }}</label>
              <BrightnessSlider
                :model-value="lampStore.getKnockoutBrightness(ledIndex - 1)"
                @update:model-value="(value) => lampStore.updateKnockoutPixel(ledIndex - 1, value)"
                :id="`knockout-pixel-${ledIndex - 1}`"
                :min="0"
                :max="100"
                append="%"
                :disabled="lampStore.disabled"
              />
            </div>
          </div>
        </CollapsiblePanel>
      </template>

      <!-- Backup & Restore slot -->
      <template #backupControls>
        <div class="backup-controls">
          <div class="backup-buttons">
            <button
              type="button"
              class="backup-btn backup-btn--export"
              :disabled="lampStore.hasChanges || lampStore.disabled"
              @click="handleExport"
            >
              <span class="backup-btn-icon">↓</span>
              Export Backup
            </button>
            <button
              type="button"
              class="backup-btn backup-btn--restore"
              :disabled="lampStore.hasChanges || lampStore.disabled"
              @click="handleRestoreClick"
            >
              <span class="backup-btn-icon">↑</span>
              Restore Backup
            </button>
            <input
              ref="fileInputRef"
              type="file"
              accept=".json,application/json"
              class="backup-file-input"
              @change="handleFileSelect"
            />
          </div>
          <p v-if="lampStore.hasChanges" class="backup-warning">
            Save or discard pending changes before exporting or restoring.
          </p>
          <p v-if="restoreError" class="backup-error">
            {{ restoreError }}
          </p>
          <p v-if="restoreSuccess" class="backup-success">
            Backup restored successfully. Click "Save Changes" to apply.
          </p>
        </div>
      </template>
    </ComponentForm>
  </section>
</template>

<style scoped>
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

.backup-buttons {
  display: flex;
  gap: 12px;
  flex-wrap: wrap;
}

.backup-btn {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  border: 1px solid var(--color-border);
  border-radius: 6px;
  background: var(--color-background-mute);
  color: var(--brand-lamp-white);
  font-size: 0.85rem;
  font-weight: 500;
  cursor: pointer;
  transition: all 0.2s ease;
}

.backup-btn:hover:not(:disabled) {
  background: var(--color-background-soft);
  border-color: var(--color-border-hover);
}

.backup-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.backup-btn--export:hover:not(:disabled) {
  border-color: var(--brand-aurora-blue);
  color: var(--brand-aurora-blue);
}

.backup-btn--restore:hover:not(:disabled) {
  border-color: var(--brand-lumen-green);
  color: var(--brand-lumen-green);
}

.backup-btn-icon {
  font-size: 1rem;
  font-weight: bold;
}

.backup-file-input {
  display: none;
}

.backup-warning {
  margin-top: 12px;
  padding: 10px 12px;
  background: rgba(225, 164, 74, 0.1);
  border: 1px solid rgba(225, 164, 74, 0.3);
  border-radius: 6px;
  color: var(--brand-amber-gold);
  font-size: 0.8rem;
  line-height: 1.4;
}

.backup-error {
  margin-top: 12px;
  padding: 10px 12px;
  background: rgba(248, 113, 113, 0.1);
  border: 1px solid rgba(248, 113, 113, 0.3);
  border-radius: 6px;
  color: var(--color-error);
  font-size: 0.8rem;
  line-height: 1.4;
}

.backup-success {
  margin-top: 12px;
  padding: 10px 12px;
  background: rgba(141, 205, 166, 0.1);
  border: 1px solid rgba(141, 205, 166, 0.3);
  border-radius: 6px;
  color: var(--brand-lumen-green);
  font-size: 0.8rem;
  line-height: 1.4;
}

/* Mobile optimizations for backup controls */
@media (max-width: 480px) {
  .backup-buttons {
    flex-direction: column;
  }

  .backup-btn {
    width: 100%;
    justify-content: center;
  }
}
</style>
