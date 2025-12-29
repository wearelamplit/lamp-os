<script setup lang="ts">
import { ref, computed } from 'vue'
import ComponentForm from '@/components/Form.vue'
import BrightnessSlider from '@/components/BrightnessSlider.vue'
import type { FieldDefinition, FormValues } from '@/types'
import { useLampStore, MAX_LEDS_BASE } from '@/stores/lamp'

const lampStore = useLampStore()

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
</style>
