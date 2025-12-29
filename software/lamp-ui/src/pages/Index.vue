<!-- eslint-disable vue/multi-word-component-names -->
<script setup lang="ts">
import { computed, watch } from 'vue'
import ComponentForm from '@/components/Form.vue'
import CritterNameplate from '@/components/CritterNameplate.vue'
import InfoPanel from '@/components/InfoPanel.vue'
import type { FieldDefinition, FormValues } from '@/types'
import { useLampStore } from '@/stores/lamp'

const lampStore = useLampStore()

// Field definitions for the home page form
const fields = computed<FieldDefinition[]>(() => [
  {
    name: 'nameplate',
    type: 'slot',
    label: 'Nameplate',
  },
  {
    name: 'brightnessHeading',
    type: 'group-heading',
    label: 'Lamp Brightness',
  },
  {
    name: 'homeModeNotice',
    type: 'slot',
    label: 'Home Mode Notice',
  },
  {
    name: 'brightness',
    type: 'brightness-slider',
    label: 'Brightness',
    default: 100,
    optional: true,
    // show: (values: FormValues) => !values.homeMode,
    props: {
      min: 0,
      max: 100,
      step: 1,
      append: '%',
    },
  },
  {
    name: 'colorHeading',
    type: 'group-heading',
    label: 'Lamp Color Settings',
  },
  {
    name: 'shadeColors',
    type: 'color-list',
    label: 'Shade',
    default: ['#FF0000FF'],
    optional: true,
    props: {
      max: 1,
      showAddButton: false,
      isGradient: false,
      hasActiveColor: false,
    },
  },
  {
    name: 'baseColors',
    type: 'color-list',
    label: 'Base',
    default: ['#FF0000FF'],
    optional: true,
    props: {
      max: 5,
      showAddButton: true,
      isGradient: true,
      hasActiveColor: true,
      activeColor: lampStore.state.base?.ac ?? 0,
    },
  },
])

// Map store state to form values
const formValues = computed({
  get: () => ({
    brightness: lampStore.state.lamp?.brightness ?? 100,
    homeMode: lampStore.state.lamp?.homeMode ?? false,
    shadeColors: lampStore.state.shade?.colors ?? ['#FF0000FF'],
    baseColors: lampStore.state.base?.colors ?? ['#FF0000FF'],
    baseActiveColor: lampStore.state.base?.ac ?? 0,
  }),
  set: () => {
    // Values are updated via individual handlers
  },
})

// Handle form value changes
const handleFormUpdate = (values: FormValues) => {
  if (values.brightness !== undefined && values.brightness !== formValues.value.brightness) {
    lampStore.updateBrightness(values.brightness as number)
  }
  if (values.shadeColors !== undefined) {
    lampStore.updateShadeColors(values.shadeColors as string[])
  }
  if (values.baseColors !== undefined) {
    lampStore.updateBaseColors(values.baseColors as string[])
  }
}

// Handle meta events from form fields
const handleFormMeta = (fieldName: string, value: unknown) => {
  if (fieldName === 'baseColors' && value && typeof value === 'object') {
    const meta = value as { activeColor?: number }
    if (meta.activeColor !== undefined) {
      lampStore.updateBaseActiveColor(meta.activeColor)
    }
  }
}

// Watch for color changes (for real-time updates)
watch(
  () => formValues.value.shadeColors,
  (newColors) => {
    if (newColors) {
      lampStore.updateShadeColors(newColors)
    }
  },
  { deep: true },
)

watch(
  () => formValues.value.baseColors,
  (newColors) => {
    if (newColors) {
      lampStore.updateBaseColors(newColors)
    }
  },
  { deep: true },
)
</script>

<template>
  <section class="tab-panel" aria-label="Home settings">
    <ComponentForm
      :fields="fields"
      :model-value="formValues"
      @update:model-value="handleFormUpdate"
      @meta="handleFormMeta"
      :show-button="false"
      :disabled="lampStore.disabled"
    >
      <!-- Nameplate slot -->
      <template #nameplate>
        <CritterNameplate v-model="lampStore.state" />
      </template>

      <!-- Home mode notice slot -->
      <template #homeModeNotice>
        <InfoPanel v-if="lampStore.state.lamp?.homeMode">
          Home Mode is on which uses it's own brightness setting.
        </InfoPanel>
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

</style>
